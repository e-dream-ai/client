#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <sio_client.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <cstdio>

#include "ContentDownloader.h"
#include "StringFormat.h"
#include "Log.h"
#include "Player.h"
#include "Networking.h"
#include "Settings.h"
#include "Shepherd.h"
#include "client.h"
#include "clientversion.h"
#include "EDreamClient.h"
#include "JSONUtil.h"

static sio::client s_SIOClient;

namespace json = boost::json;
using namespace ContentDownloader;

static void OnWebSocketMessage(sio::event& event);

static ShowPreferencesCallback_t gShowPreferencesCallback = nullptr;
void ESSetShowPreferencesCallback(ShowPreferencesCallback_t _callback)
{
    gShowPreferencesCallback = _callback;
}
void ESShowPreferences()
{
    if (gShowPreferencesCallback != nullptr)
    {
        gShowPreferencesCallback();
    }
}
std::atomic<char*> EDreamClient::fAccessToken(nullptr);
std::atomic<char*> EDreamClient::fRefreshToken(nullptr);
std::atomic<bool> EDreamClient::fIsLoggedIn(false);
std::atomic<int> EDreamClient::fCpuUsage(0);
std::mutex EDreamClient::fAuthMutex;
bool fIsWebSocketConnected = true;

static void SetNewAndDeleteOldString(std::atomic<char*>& str, char* newval)
{
    char* toDelete = str.exchange(newval);

    if (toDelete != nullptr)
        delete[] toDelete;
}

static void SetNewAndDeleteOldString(
    std::atomic<char*>& str, const char* newval,
    boost::memory_order mem_ord = boost::memory_order_relaxed)
{
    if (newval == nullptr)
    {
        SetNewAndDeleteOldString(str, (char*)nullptr, mem_ord);
        return;
    }
    size_t len = strlen(newval) + 1;
    char* newStr = new char[len];
    memcpy(newStr, newval, len);
    SetNewAndDeleteOldString(str, newStr);
}

static void BindWebSocketCallbacks()
{
    s_SIOClient.socket("/remote-control")
        ->on("new_remote_control_event", &OnWebSocketMessage);
}

static void OnWebSocketConnected()
{

    //    std::map<std::string, std::string> params;
    //    params["event"] = "next
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "next");
    sio::message::list list;
    list.push(ms);
    s_SIOClient.socket("/remote-control")
        ->emit("new_remote_control_event", list);
}

static void OnWebSocketClosed(const sio::client::close_reason& _reason)
{
    g_Log->Info("WebSocket connection closed.");
}

static void OnWebSocketFail() { g_Log->Error("WebSocket connection failed."); }

static void OnWebSocketReconnecting()
{
    g_Log->Error("WebSocket connection failed.");
}

static void OnWebSocketReconnect(unsigned _num, unsigned _delay)
{
    g_Log->Error("WebSocket connection failed.");
}

void EDreamClient::InitializeClient()
{
    s_SIOClient.set_open_listener(&OnWebSocketConnected);
    s_SIOClient.set_close_listener(&OnWebSocketClosed);
    s_SIOClient.set_fail_listener(&OnWebSocketFail);
    s_SIOClient.set_reconnecting_listener(&OnWebSocketReconnecting);
    s_SIOClient.set_reconnect_listener(&OnWebSocketReconnect);

    SetNewAndDeleteOldString(
        fAccessToken,
        g_Settings()
            ->Get("settings.content.access_token", std::string(""))
            .c_str());
    SetNewAndDeleteOldString(
        fRefreshToken,
        g_Settings()
            ->Get("settings.content.refresh_token", std::string(""))
            .c_str());
    fAuthMutex.lock();
    boost::thread authThread(&EDreamClient::Authenticate);
}

void EDreamClient::DeinitializeClient()
{
    s_SIOClient.set_open_listener(nullptr);
    s_SIOClient.set_close_listener(nullptr);
    s_SIOClient.set_fail_listener(nullptr);
    s_SIOClient.set_reconnecting_listener(nullptr);
    s_SIOClient.set_reconnect_listener(nullptr);
}

const char* EDreamClient::GetAccessToken() { return fAccessToken.load(); }

bool EDreamClient::Authenticate()
{
    PlatformUtils::SetThreadName("Authenticate");
    bool success = false;
    if (GetAccessToken() && strlen(GetAccessToken()))
    {
        Network::spCFileDownloader spDownload =
            std::make_shared<Network::CFileDownloader>("Authenticate");
        spDownload->AppendHeader("Content-Type: application/json");

        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};

        spDownload->AppendHeader(authHeader);
        success = spDownload->Perform(Shepherd::GetEndpoint(ENDPOINT_USER));
        if (!success)
        {
            if (spDownload->ResponseCode() == 401)
            {
                success = RefreshAccessToken();
            }
            else
            {
                g_Log->Error("Authentication failed. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }
    else
    {
        g_Log->Error("Authentication failed. Access token was not set.");
    }
    if (!success)
    {
        ESShowPreferences();
    }
    fIsLoggedIn.exchange(success);
    fAuthMutex.unlock();
    if (success)
    {
        boost::thread webSocketThread(
            &EDreamClient::ConnectRemoteControlSocket);
    }
    return success;
}

void EDreamClient::SignOut()
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    fIsLoggedIn.exchange(false);
    SetNewAndDeleteOldString(fAccessToken, "");
    SetNewAndDeleteOldString(fRefreshToken, "");
    g_Settings()->Set("settings.content.access_token", std::string(""));
    g_Settings()->Set("settings.content.refresh_token", std::string(""));
    g_Settings()->Storage()->Commit();
}

void EDreamClient::DidSignIn(const std::string& _authToken,
                             const std::string& _refreshToken)
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    fIsLoggedIn.exchange(true);
    SetNewAndDeleteOldString(fAccessToken, _authToken.c_str());
    SetNewAndDeleteOldString(fRefreshToken, _refreshToken.c_str());
    g_Settings()->Set("settings.content.access_token", _authToken);
    g_Settings()->Set("settings.content.refresh_token", _refreshToken);
    g_Settings()->Storage()->Commit();
    boost::thread webSocketThread(&EDreamClient::ConnectRemoteControlSocket);
}

bool EDreamClient::IsLoggedIn()
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    return fIsLoggedIn.load();
}

bool EDreamClient::RefreshAccessToken()
{
    std::string body{
        string_format("{\"refreshToken\":\"%s\"}", fRefreshToken.load())};
    Network::spCFileDownloader spDownload =
        std::make_shared<Network::CFileDownloader>("Refresh token");
    spDownload->AppendHeader("Content-Type: application/json");
    spDownload->AppendHeader("Accept: application/json");
    spDownload->SetPostFields(body.data());
    if (spDownload->Perform(Shepherd::GetEndpoint(ENDPOINT_REFRESH)))
    {
        json::error_code ec;
        json::value response = json::parse(spDownload->Data(), ec);
        json::value data = response.at("data");
        const char* accessToken = data.at("AccessToken").as_string().data();
        g_Settings()->Set("settings.content.access_token",
                          std::string(accessToken));
        SetNewAndDeleteOldString(fAccessToken, accessToken);
        g_Settings()->Storage()->Commit();
        return true;
    }
    else
    {
        if (spDownload->ResponseCode() == 400)
        {
            g_Settings()->Set("settings.content.access_token", std::string(""));
            ESShowPreferences();
            g_Settings()->Storage()->Commit();
            return false;
        }
        g_Log->Error("Refreshing access token failed. Server returned %i: %s",
                     spDownload->ResponseCode(), spDownload->Data().c_str());
        return false;
    }
}

bool EDreamClient::GetDreams(int _page, int _count)
{
    Network::spCFileDownloader spDownload;
    const char* jsonPath = Shepherd::jsonPath();

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("Dreams list");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        std::string url{string_format(
            "%s?take=%i&skip=%i", Shepherd::GetEndpoint(ENDPOINT_DREAM),
            DREAMS_PER_PAGE, DREAMS_PER_PAGE * _page)};
        if (spDownload->Perform(url))
        {
            break;
        }
        else
        {
            if (spDownload->ResponseCode() == 400 ||
                spDownload->ResponseCode() == 401)
            {
                if (currentAttempt == maxAttempts)
                    return false;
                if (!RefreshAccessToken())
                    return false;
            }
            else
            {
                g_Log->Error("Failed to get dreams. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    std::string filename{string_format("%sdreams_%i.json", jsonPath, _page)};
    if (!spDownload->Save(filename))
    {
        g_Log->Error("Unable to save %s\n", filename.data());
        return false;
    }
    if (_count == -1)
    {
        try
        {
            json::value response = json::parse(spDownload->Data());
            json::value data = response.at("data");
            _count = (int)data.at("count").as_int64();
        }
        catch (const boost::system::system_error& e)
        {
            JSONUtil::LogException(e, spDownload->Data());
        }
    }
    if ((_page + 1) * DREAMS_PER_PAGE < _count)
    {
        if (!GetDreams(_page + 1, _count))
            return false;
    }
    return true;
}

static void OnWebSocketMessage(sio::event& _wsEvent)
{

    std::shared_ptr<sio::object_message> objectMessage =
        std::dynamic_pointer_cast<sio::object_message>(_wsEvent.get_message());
    std::map<std::string, sio::message::ptr> response =
        objectMessage->get_map();
    std::shared_ptr<sio::string_message> eventObj =
        std::dynamic_pointer_cast<sio::string_message>(response["event"]);
    std::string_view event = eventObj->get_string();

    g_Log->Info("Received WebSocket message: %s", event.data());
    if (event == "next")
    {
        g_Player().SkipToNext();
    }
    else if (event == "previous")
    {
        g_Player().ReturnToPrevious();
    }
    else if (event == "playback_slower")
    {
        g_Player().SkipForward(10);
    }
    else if (event == "playback_faster")
    {
        g_Player().SkipForward(-10);
    }
    else
    {
        g_Log->Error("Unknown event type received: %s", event.data());
    }
}

void EDreamClient::ConnectRemoteControlSocket()
{
    PlatformUtils::SetThreadName("ConnectRemoteControl");
    BindWebSocketCallbacks();
    std::map<std::string, std::string> query;
    query["token"] = string_format("Bearer %s", GetAccessToken());
    s_SIOClient.connect(Shepherd::GetWebsocketServer(), query);
    return;
}

void EDreamClient::SetCPUUsage(int _cpuUsage) { fCpuUsage.exchange(_cpuUsage); }
