#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <sio_client.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>

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

#include "client.h"

static sio::client s_SIOClient;

namespace json = boost::json;
using namespace ContentDownloader;

static void OnWebSocketMessage(sio::event& event);

// TODO: this is imperfect, temporary until we clean up the connection callback thing
static bool shownSettingsOnce = false;

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
    if (strlen(GetAccessToken()))
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
    // Don't loop open settings
    /*if (!success)
    {
        ESShowPreferences();
    }*/
    fIsLoggedIn.exchange(success);
    fAuthMutex.unlock();
    g_Log->Info("Login success:%s", success ? "true" : "false");
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
            // Only show once at startup, we don't want to loop
            if (!shownSettingsOnce) {
                shownSettingsOnce = true;
                ESShowPreferences();
            }
            g_Settings()->Storage()->Commit();
            return false;
        }
        g_Log->Error("Refreshing access token failed. Server returned %i: %s",
                     spDownload->ResponseCode(), spDownload->Data().c_str());
        return false;
    }
}


int EDreamClient::GetCurrentServerPlaylist() {
    Network::spCFileDownloader spDownload;
    const char* jsonPath = Shepherd::jsonPath();

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("CurrentPlaylist");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        
        
        std::string url{ Shepherd::GetEndpoint(ENDPOINT_CURRENTPLAYLIST) };
        
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
                    return -1;
                if (!RefreshAccessToken())
                    return -1;
            }
            else
            {
                g_Log->Error("Failed to get playlist. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    // Grab the ID from the playlist
    try
    {
        json::value response = json::parse(spDownload->Data());
        json::value data = response.at("data");
        json::value playlist = data.at("playlist");
        json::value id = playlist.at("id");

        auto idint = id.as_int64();
        
        std::string filename{string_format("%splaylist_%i.json", jsonPath, idint)};
        if (!spDownload->Save(filename))
        {
            g_Log->Error("Unable to save %s\n", filename.data());
            return -1;
        }
        
        return idint;
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, spDownload->Data());
    }
    
    return 0;

}

bool EDreamClient::FetchPlaylist(int id) {
    Network::spCFileDownloader spDownload;
    const char* jsonPath = Shepherd::jsonPath();

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("Playlist");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        
        
        std::string url{string_format(
            "%s/%i", Shepherd::GetEndpoint(ENDPOINT_PLAYLIST), id)};
        
        printf("url : %s\n", url.c_str());
        
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
                g_Log->Error("Failed to get playlist. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    std::string filename{string_format("%splaylist_%i.json", jsonPath, id)};
    if (!spDownload->Save(filename))
    {
        g_Log->Error("Unable to save %s\n", filename.data());
        return false;
    }
    
    return true;
}

std::vector<std::string> EDreamClient::ParsePlaylist(int id) {
    // Collect all UUIDS from the json
    std::vector<std::string> uuids;


    // Open playlist and grab content
    std::string filePath{
        string_format("%splaylist_%i.json", Shepherd::jsonPath(), id)};
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.data());
        return uuids;
    }
    std::string contents{(std::istreambuf_iterator<char>(file)),
        (std::istreambuf_iterator<char>())};
    file.close();
    
    
    try
    {
        json::error_code ec;
        json::value response = json::parse(contents, ec);
        json::value data = response.at("data");
        json::value playlist = data.at("playlist");
        json::value items = playlist.at("items");
        
        if (items.is_array()) {
            for (auto& item : items.as_array()) {
                json::value dreamItem = item.at("dreamItem");
                json::value uuid = dreamItem.at("uuid");
                uuids.push_back(uuid.as_string().c_str());
            }
        }
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, contents);
    }
    
    return uuids;
}

std::tuple<std::string, std::string> EDreamClient::ParsePlaylistCredits(int id) {
    // Open playlist and grab content
    std::string filePath{
        string_format("%splaylist_%i.json", Shepherd::jsonPath(), id)};
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.data());
        return {"",""};
    }
    std::string contents{(std::istreambuf_iterator<char>(file)),
        (std::istreambuf_iterator<char>())};
    file.close();
    
    try
    {
        json::error_code ec;
        json::value response = json::parse(contents, ec);
        json::value data = response.at("data");
        json::value playlist = data.at("playlist");
        
        json::value name = playlist.at("name");
        json::value user = playlist.at("user");
        json::value userName = user.at("name");

        return {name.as_string().c_str(), userName.as_string().c_str()};
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, contents);
    }
    
    return {"",""};
}


bool EDreamClient::EnqueuePlaylist(int id) {
    // Fetch the playlist and save it to disk
    if (EDreamClient::FetchPlaylist(id)) {
        // Parse the playlist
        auto uuids = EDreamClient::ParsePlaylist(id);

        if (uuids.size() > 0) {
            // save the current playlist id, this will get reused at next startup
            g_Settings()->Set("settings.content.current_playlist", id);
            g_Player().ResetPlaylist();
            return true;
        }
    }

    return false;
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
    printf("Received websocket message: %s", event.data());
    
    if (event == "play_dream") {
        std::shared_ptr<sio::string_message> uuidObj =
            std::dynamic_pointer_cast<sio::string_message>(response["uuid"]);
        std::string_view uuid = uuidObj->get_string();
        printf("should play : %s", uuid.data());

        g_Player().PlayDreamNow(uuid.data());

    } else if (event == "play_playlist") {
        std::shared_ptr<sio::int_message> idObj =
            std::dynamic_pointer_cast<sio::int_message>(response["id"]);
        auto id = idObj->get_int();
        printf("should play : %lld", id);
        
        EDreamClient::EnqueuePlaylist(id);
    }
    else if (event == "like")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_LIKE);
    }
    else if (event == "dislike")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_DISLIKE);
    }
    else if (event == "next")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_NEXT);
    }
    else if (event == "previous")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PREVIOUS);
    }
    else if (event == "repeat")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_REPEAT);
    }
    else if (event == "forward")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SKIP_FW);
    }
    else if (event == "backward")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SKIP_BW);
    }
    else if (event == "playback_slower")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PLAYBACK_SLOWER);
    }
    else if (event == "playback_faster")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PLAYBACK_FASTER);
    }
    else if (event == "repeat")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_REPEAT);
    }
    else if (event == "help")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_F1);
    }
    else if (event == "status")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_F2);
    }
    else if (event == "pause")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PAUSE);
    }
    else if (event == "credit")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_CREDIT);
    }
    else if (event == "reset_playlist")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_RESET_PLAYLIST);
    }
    else if (event == "web")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_WEBPAGE);
    }
    else if (event == "brighter")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_BRIGHTNESS_UP);
    }
    else if (event == "darker")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_BRIGHTNESS_DOWN);
    }
    else if (event == "set_speed_1")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_1);
    }
    else if (event == "set_speed_2")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_2);
    }
    else if (event == "set_speed_3")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_3);
    }
    else if (event == "set_speed_4")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_4);
    }
    else if (event == "set_speed_5")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_5);
    }
    else if (event == "set_speed_6")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_6);
    }
    else if (event == "set_speed_7")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_7);
    }
    else if (event == "set_speed_8")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_8);
    }
    else if (event == "set_speed_9")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_9);
    }
    else
    {
        g_Log->Error("Unknown event type received: %s", event.data());
    }
}

void EDreamClient::SendPlayingDream(std::string uuid) {// ) {
    std::cout << "Sending UUID " << uuid;
    
    
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "playing");
    ms->insert("uuid", uuid);

    sio::message::list list;
    list.push(ms);
    s_SIOClient.socket("/remote-control")
        ->emit("new_remote_control_event", list);
}

void EDreamClient::ConnectRemoteControlSocket()
{
    PlatformUtils::SetThreadName("ConnectRemoteControl");
    g_Log->Info("Performing remote control connect.");
    BindWebSocketCallbacks();
    std::map<std::string, std::string> query;
    query["token"] = string_format("Bearer %s", GetAccessToken());
    s_SIOClient.connect(Shepherd::GetWebsocketServer(), query);
}

void EDreamClient::SetCPUUsage(int _cpuUsage) { fCpuUsage.exchange(_cpuUsage); }

