#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
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

typedef websocketpp::client<websocketpp::config::asio_client> WebSocketClient;
static void OnWebSocketMessage(websocketpp::connection_hdl,
                               WebSocketClient::message_ptr message);
static void OnWebSocketOpen(websocketpp::connection_hdl);
static void OnWebSocketClose(websocketpp::connection_hdl);
static void OnWebSocketFail(websocketpp::connection_hdl);

namespace json = boost::json;
using namespace ContentDownloader;

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
boost::atomic<char*> EDreamClient::fAccessToken(nullptr);
boost::atomic<char*> EDreamClient::fRefreshToken(nullptr);
boost::atomic<bool> EDreamClient::fIsLoggedIn(false);
boost::atomic<int> EDreamClient::fCpuUsage(0);
boost::mutex EDreamClient::fAuthMutex;
WebSocketClient s_WebSocketClient;

static void SetNewAndDeleteOldString(
    boost::atomic<char*>& str, char* newval,
    boost::memory_order mem_ord = boost::memory_order_relaxed)
{
    char* toDelete = str.exchange(newval, mem_ord);

    if (toDelete != nullptr)
        delete[] toDelete;
}

static void SetNewAndDeleteOldString(
    boost::atomic<char*>& str, const char* newval,
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

void EDreamClient::InitializeClient()
{
    // g_Settings()->Set("settings.content.access_token", std::string(""));
    //    s_WebSocketClient.set_access_channels(websocketpp::log::alevel::all);
    //    s_WebSocketClient.clear_access_channels(
    //        websocketpp::log::alevel::frame_payload);
    //    s_WebSocketClient.set_error_channels(websocketpp::log::elevel::all);
    //    s_WebSocketClient.init_asio();
    //    s_WebSocketClient.set_message_handler(&OnWebSocketMessage);
    //    s_WebSocketClient.set_open_handler(&OnWebSocketOpen);
    //    s_WebSocketClient.set_close_handler(&OnWebSocketClose);
    //    s_WebSocketClient.set_fail_handler(&OnWebSocketFail);

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
    //boost::thread webSocketThread(&EDreamClient::ConnectRemoteControlSocket);
}

const char* EDreamClient::GetAccessToken()
{
    return fAccessToken.load(boost::memory_order_relaxed);
}

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
    fIsLoggedIn.exchange(success);
    fAuthMutex.unlock();
    return success;
}

void EDreamClient::SignOut()
{
    boost::lock_guard<boost::mutex> lock(fAuthMutex);
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
    boost::lock_guard<boost::mutex> lock(fAuthMutex);
    fIsLoggedIn.exchange(true);
    SetNewAndDeleteOldString(fAccessToken, _authToken.c_str());
    SetNewAndDeleteOldString(fRefreshToken, _refreshToken.c_str());
    g_Settings()->Set("settings.content.access_token", _authToken);
    g_Settings()->Set("settings.content.refresh_token", _refreshToken);
    g_Settings()->Storage()->Commit();
}

bool EDreamClient::IsLoggedIn()
{
    boost::lock_guard<boost::mutex> lock(fAuthMutex);
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

bool EDreamClient::GetDreams()
{
    int page = g_Settings()->Get("settings.content.dreams_page", 0);
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
        constexpr int DREAMS_PER_PAGE = 10;
        if (spDownload->Perform(string_format(
                "%s?take=%i&skip=%i", Shepherd::GetEndpoint(ENDPOINT_DREAM),
                DREAMS_PER_PAGE, DREAMS_PER_PAGE * page)))
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

    std::string filename{string_format("%sdreams_%i.json", jsonPath, page)};
    if (!spDownload->Save(filename))
    {
        g_Log->Error("Unable to save %s\n", filename.data());
        return false;
    }
    return true;
}

static void OnWebSocketMessage(websocketpp::connection_hdl,
                               WebSocketClient::message_ptr _pMessage)
{
    std::string_view message = _pMessage->get_payload();
    g_Log->Info("Received WebSocket message: %s", message.data());
    try
    {

        json::error_code ec;
        json::value response = json::parse(message, ec);
        std::string_view event = response.at("event").as_string().data();
        if (event == "next")
        {
            g_Player().SkipToNext();
        }
        else if (event == "previous")
        {
            g_Player().ReturnToPrevious();
        }
        else if (event == "skip_fw")
        {
            g_Player().SkipForward(10);
        }
        else if (event == "skip_bw")
        {
            g_Player().SkipForward(-10);
        }
        else
        {
            g_Log->Error("Unknown event type received: %s (message was:%s)",
                         event.data(), message.data());
        }
    }
    catch (std::exception const& e)
    {
        g_Log->Error(
            "Exception while parsing WebSocket message: %s (message was:%s)",
            e.what(), message.data());
    }
}

static void OnWebSocketOpen(websocketpp::connection_hdl handle)
{
    try
    {
        s_WebSocketClient.send(handle,
                               "{\"event\":\"authentication\",\"client_id\":"
                               "\"client\",\"client_mode\":\"desktop\"}",
                               websocketpp::frame::opcode::value::TEXT);
    }
    catch (websocketpp::exception const& ex)
    {
        g_Log->Error("WebSocket exception: %s", ex.what());
    }
}

static void OnWebSocketClose(websocketpp::connection_hdl handle)
{
    std::string reason =
        s_WebSocketClient.get_con_from_hdl(handle)->get_remote_close_reason();
    g_Log->Error("WebSocket closed. Reason: %s", reason.data());
}

static void OnWebSocketFail(websocketpp::connection_hdl handle)
{
    std::string reason =
        s_WebSocketClient.get_con_from_hdl(handle)->get_remote_close_reason();
    g_Log->Error("WebSocket failure. Reason: %s", reason.data());
}

void EDreamClient::ConnectRemoteControlSocket()
{
    PlatformUtils::SetThreadName("ConnectRemoteControl");
    try
    {
        websocketpp::lib::error_code ec;
        WebSocketClient::connection_ptr connection =
            s_WebSocketClient.get_connection("ws://localhost:9000/ws", ec);
        if (ec)
        {
            g_Log->Error("Error creating WebSocket connection: %s",
                         ec.message().c_str());
            return;
        }
        s_WebSocketClient.connect(connection);
        std::string payload = "{\"event\":\"authentication\",\"client_id\":"
                              "\"client\",\"client_mode\":\"desktop\"}";
        connection->send(payload);
        boost::thread cpuUsageThread(
            [=]() -> void
            {
                while (true)
                {
                    boost::thread::sleep(boost::get_system_time() +
                                         boost::posix_time::seconds(1));
                    int cpuUsage = fCpuUsage.load();
                    connection->send(string_format(
                        "{\"event\":\"cpu\",\"value\":%d}", cpuUsage));
                }
            });
        s_WebSocketClient.run();
    }
    catch (websocketpp::exception const& ex)
    {
        g_Log->Error("WebSocket exception: %s", ex.what());
    }
}

void EDreamClient::SetCPUUsage(int _cpuUsage) { fCpuUsage.exchange(_cpuUsage); }
