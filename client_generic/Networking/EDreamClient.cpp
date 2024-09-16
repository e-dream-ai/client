#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <sio_client.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>
//#include <boost/json/src.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <future>

#include "ContentDownloader.h"
#include "StringFormat.h"
#include "Log.h"
#include "Player.h"
#include "Networking.h"
#include "Settings.h"
#include "ServerConfig.h"
#include "PathManager.h"
#include "client.h"
#include "clientversion.h"
#include "EDreamClient.h"
#include "JSONUtil.h"
#include "CacheManager.h"

#include "client.h"

static sio::client s_SIOClient;

namespace json = boost::json;
using namespace ContentDownloader;

class ParserHelper {
public:
    static std::vector<std::string> ParseSubPlaylist(boost::json::object item);
};

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

long long EDreamClient::remainingQuota = 0;
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


std::unique_ptr<boost::asio::io_context> EDreamClient::io_context = std::make_unique<boost::asio::io_context>();
std::unique_ptr<boost::asio::steady_timer> EDreamClient::ping_timer = std::make_unique<boost::asio::steady_timer>(*io_context);

// MARK: Ping via websocket
void EDreamClient::SendPing()
{
    s_SIOClient.socket("/remote-control")->emit("ping");
    g_Log->Info("Ping sent");
    ScheduleNextPing();
}

void EDreamClient::ScheduleNextPing()
{
    ping_timer->expires_after(std::chrono::seconds(30));
    ping_timer->async_wait([](const boost::system::error_code& ec) {
        if (!ec) {
            SendPing();
        }
    });
}

void EDreamClient::SendGoodbye()
{
    if (!s_SIOClient.opened())
    {
        g_Log->Info("WebSocket not connected, skipping goodbye message");
        return;
    }

    s_SIOClient.socket("/remote-control")->emit("goodbye");
    g_Log->Info("Goodbye message sent");
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
    // Stop the timer and io_context
    ping_timer->cancel();
    io_context->stop();

    // Send goodbye message
    SendGoodbye();

    // Close the WebSocket connection
    s_SIOClient.close();
    
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
            std::make_shared<Network::CFileDownloader>("Authentication");
        spDownload->AppendHeader("Content-Type: application/json");

        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};

        spDownload->AppendHeader(authHeader);
        success = spDownload->Perform(ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::USER));
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
    if (spDownload->Perform(ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::REFRESH)))
    {
        boost::system::error_code ec;
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

// MARK: - Auth v2
bool EDreamClient::SendCode()
{
    std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
    
    if (email.empty())
    {
        g_Log->Error("Email address not found in settings");
        return false;
    }
    
    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("Send Login Code");
    spDownload->AppendHeader("Content-Type: application/json");
    
    // Prepare the JSON payload
    boost::json::object payload;
    payload["email"] = email;
    std::string body = boost::json::serialize(payload);
    
    spDownload->SetPostFields(body.data());
    g_Log->Info("server : %s", ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC).c_str());
    
    if (spDownload->Perform(ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC)))
    {
        if (spDownload->ResponseCode() == 200)
        {
            g_Log->Info("Login code sent successfully to %s", email.c_str());
            return true;
        }
        else
        {
            g_Log->Error("Failed to send login code. Server returned %i: %s",
                         spDownload->ResponseCode(), spDownload->Data().c_str());
            return false;
        }
    }
    else
    {
        g_Log->Error("Network error while sending login code");
        return false;
    }
}

bool EDreamClient::ValidateCode(const std::string& code)
{
    std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
    
    if (email.empty())
    {
        g_Log->Error("Email address not found in settings");
        return false;
    }

    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("Validate Login Code");
    spDownload->AppendHeader("Content-Type: application/json");

    // Prepare the JSON payload
    boost::json::object payload;
    payload["email"] = email;
    payload["code"] = code; 
    std::string body = boost::json::serialize(payload);

    spDownload->SetPostFields(body.data());

    if (spDownload->Perform(ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC)))
    {
        if (spDownload->ResponseCode() == 200)
        {
            try
            {
                boost::json::value response = boost::json::parse(spDownload->Data());
                boost::json::object responseObj = response.as_object();

                if (responseObj.contains("sealedSession") && responseObj["sealedSession"].is_string())
                {
                    std::string sealedSession = responseObj["sealedSession"].as_string().c_str();
                    g_Settings()->Set("settings.content.sealed_session", sealedSession);
                    g_Settings()->Storage()->Commit();  // Save the settings

                    g_Log->Info("Sealed session saved successfully");
                }
                else
                {
                    g_Log->Error("sealedSession not found in the response");
                    return false;
                }

                if (responseObj.contains("user") && responseObj["user"].is_object())
                {
                    boost::json::object userObj = responseObj["user"].as_object();
                    g_Log->Info("User object received: %s", boost::json::serialize(userObj).c_str());
                }
                else
                {
                    g_Log->Warning("User object not found in the response");
                }

                return true;
            }
            catch (const boost::json::system_error& e)
            {
                g_Log->Error("JSON parsing error: %s", e.what());
                return false;
            }
        }
        else
        {
            g_Log->Error("Failed to validate login code. Server returned %i: %s",
                         spDownload->ResponseCode(), spDownload->Data().c_str());
            return false;
        }
    }
    else
    {
        g_Log->Error("Network error while validating login code");
        return false;
    }
}


// MARK: - Hello call
// Post auth initial handshake with server
//
//
// Returns a JSON with a data structure containing
//      "quota": int,   // in bytes
//      "currentPlaylistId": int    // playlistID
std::string EDreamClient::Hello() {
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    Network::spCFileDownloader spDownload;

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("Hello!");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        
        std::string url{ ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::HELLO) };
        
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
                    return "";
                if (!RefreshAccessToken())
                    return "";
            }
            else
            {
                g_Log->Error("Failed to handshake. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }
    
    // Grab the ID and quota
    try
    {
        json::value response = json::parse(spDownload->Data());
        json::value data = response.at("data");
        json::value quota = data.at("quota");
        json::value dislikesCount = data.at("dislikesCount");

        remainingQuota = quota.as_int64();
        int serverDislikes = dislikesCount.as_int64();
        
        // Update CacheManager with that info
        cm.setRemainingQuota(remainingQuota);
        
        // Get the count of evicted UUIDs from CacheManager
        size_t localEvictedCount = Cache::CacheManager::getInstance().getEvictedUUIDsCount();

        // Compare the counts
        if (serverDislikes != localEvictedCount) {
            g_Log->Info("Mismatch between server dislikes (%d) and local evicted UUIDs (%zu)",
                        serverDislikes, localEvictedCount);

            // Fetch the full list of dislikes from the server
            std::vector<std::string> serverDislikesList = FetchUserDislikes();
            
            if (!serverDislikesList.empty()) {
                // Update CacheManager with the new list of evicted UUIDs
                Cache::CacheManager::getInstance().clearEvictedUUIDs();
                for (const auto& uuid : serverDislikesList) {
                    Cache::CacheManager::getInstance().addEvictedUUID(uuid);
                }
                
                // Save the updated evicted UUIDs
                Cache::CacheManager::getInstance().saveEvictedUUIDsToJson();
                
                g_Log->Info("Updated local evicted UUIDs with server dislikes. New count: %zu", serverDislikesList.size());
            } else {
                g_Log->Error("Failed to fetch server dislikes list. Local evicted UUIDs remain unchanged.");
            }

        } else {
            g_Log->Info("Server dislikes (%d) match local evicted UUIDs count", serverDislikes);
        }
        
        if (data.as_object().if_contains("currentPlaylistUUID")) {
            json::value currentPlaylistId = data.at("currentPlaylistUUID");
            auto uuid = currentPlaylistId.as_string();

            g_Log->Info("Handshake with server successful, playlist id : %s, remaining quota : %lld", uuid.c_str(), remainingQuota);

            return std::string(uuid);
        } else {
            g_Log->Info("Handshake with server successful, no playlist, remaining quota : %lld", remainingQuota);

            return "";
        }
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, spDownload->Data());
    }
    
    return "";
}



std::string EDreamClient::GetCurrentServerPlaylist() {
    // Handshake server and get quota and current playlist ID
    auto playlistId = Hello();
    
    // Fetch playlist if we have one and save it to disk
    if (playlistId != "") {
        FetchPlaylist(playlistId);
    } else {
        FetchDefaultPlaylist();
    }
    
    return playlistId;
}

// MARK: - Async funtions
std::future<bool> EDreamClient::FetchPlaylistAsync(const std::string& uuid) {
    return std::async(std::launch::async, [uuid]() {
        bool result = FetchPlaylist(uuid);
        if (!result) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", uuid.c_str());
        }
        return result;
    });
}

std::future<bool> EDreamClient::FetchDefaultPlaylistAsync() {
    return std::async(std::launch::async, []() {
        bool result = FetchDefaultPlaylist();
        if (!result) {
            g_Log->Error("Failed to fetch default playlist.");
        }
        return result;
    });
}

std::future<bool> EDreamClient::FetchDreamMetadataAsync(const std::string& uuid) {
    return std::async(std::launch::async, [uuid]() {
        bool result = FetchDreamMetadata(uuid);
        if (!result) {
            g_Log->Error("Failed to fetch dream metadata. UUID: %s", uuid.c_str());
        }
        return result;
    });
}

std::future<std::string> EDreamClient::GetDreamDownloadLinkAsync(const std::string& uuid) {
    return std::async(std::launch::async, [uuid]() {
        std::string link = GetDreamDownloadLink(uuid);
        if (link.empty()) {
            g_Log->Error("Failed to get dream download link. UUID: %s", uuid.c_str());
        }
        return link;
    });
}

std::future<void> EDreamClient::SendPlayingDreamAsync(const std::string& uuid) {
    return std::async(std::launch::async, [uuid]() {
        SendPlayingDream(uuid);
        g_Log->Info("Sent playing dream information. UUID: %s", uuid.c_str());
    });
}

std::future<bool> EDreamClient::EnqueuePlaylistAsync(const std::string& uuid) {
    return std::async(std::launch::async, [uuid]() {
        // First, fetch the playlist asynchronously
        auto fetchFuture = FetchPlaylistAsync(uuid);
        
        // Wait for the fetch to complete
        bool fetchSuccess = fetchFuture.get();
        
        if (!fetchSuccess) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", uuid.c_str());
            return false;
        }

        // Parse the playlist, we do this here as, in cascade, it will also fetch any dream metadata we need
        auto uuids = ParsePlaylist(uuid);
        
        if (uuids.empty()) {
            g_Log->Error("Failed to parse playlist or playlist is empty. UUID: %s", uuid.c_str());
            return false;
        }

        // save the current playlist id, this will get reused at next startup
        g_Settings()->Set("settings.content.current_playlist_uuid", uuid);
        g_Player().SetPlaylist(std::string(uuid));
        
        g_Player().SetTransitionDuration(1.0f);
        g_Player().StartTransition();
        
        return true;
    });
}

// MARK: - Synchroneous functions
std::vector<std::string> EDreamClient::FetchUserDislikes() {
    std::vector<std::string> dislikes;
    Network::spCFileDownloader spDownload;

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("Fetch User Dislikes");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDISLIKES);
        
        if (spDownload->Perform(url))
        {
            try
            {
                json::value response = json::parse(spDownload->Data());
                json::value data = response.at("data");
                json::array dislikesArray = data.at("dislikes").as_array();

                for (const auto& dislike : dislikesArray) {
                    dislikes.push_back(dislike.as_string().c_str());
                }

                return dislikes;
            }
            catch (const boost::system::system_error& e)
            {
                JSONUtil::LogException(e, spDownload->Data());
            }
            break;
        }
        else
        {
            if (spDownload->ResponseCode() == 400 ||
                spDownload->ResponseCode() == 401)
            {
                if (currentAttempt == maxAttempts)
                    return dislikes;
                if (!RefreshAccessToken())
                    return dislikes;
            }
            else
            {
                g_Log->Error("Failed to fetch user dislikes. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }
    
    return dislikes;
}


bool EDreamClient::FetchPlaylist(std::string_view uuid) {
    // Lets make it simpler
    if (uuid.empty()) {
        g_Log->Info("Rerouting to fetching default playlist");
        return FetchDefaultPlaylist();
    }
    
    Network::spCFileDownloader spDownload;
    auto jsonPath = Cache::PathManager::getInstance().jsonPlaylistPath();

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
            "%s/%s", ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETPLAYLIST).c_str(), uuid)};
        
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

    auto filename = jsonPath / ("playlist_" + std::string(uuid) + ".json");
    if (!spDownload->Save(filename.string()))
    {
        g_Log->Error("Unable to save %s\n", filename.string().c_str());
        return false;
    }
    
    return true;
}

bool EDreamClient::FetchDefaultPlaylist() {
    Network::spCFileDownloader spDownload;
    auto jsonPath = Cache::PathManager::getInstance().jsonPlaylistPath();

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("Default Playlist");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        
        std::string url{ ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDEFAULTPLAYLIST) };
        
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
                g_Log->Error("Failed to get default playlist. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    auto filename = jsonPath / "playlist_0.json";
    if (!spDownload->Save(filename.string())) {
        g_Log->Error("Unable to save %s\n", filename.string().c_str());
        return false;
    }
    
    return true;
}

bool EDreamClient::FetchDreamMetadata(std::string uuid) {
    Network::spCFileDownloader spDownload;
    auto jsonPath = Cache::PathManager::getInstance().jsonDreamPath();

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("Metadata");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);
        
        
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDREAM) +
        "?uuids=" + uuid;
       
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

    auto filename = jsonPath / (uuid + ".json");
    if (!spDownload->Save(filename.string()))
    {
        g_Log->Error("Unable to save %s\n", filename.string().c_str());
        return false;
    }
    
    return true;
}

std::string EDreamClient::GetDreamDownloadLink(const std::string& uuid) {
    Network::spCFileDownloader spDownload;
    int maxAttempts = 3;
    int currentAttempt = 0;

    while (currentAttempt++ < maxAttempts) {
        spDownload = std::make_shared<Network::CFileDownloader>("Dream Link");
        spDownload->AppendHeader("Content-Type: application/json");
        std::string authHeader{
            string_format("Authorization: Bearer %s", GetAccessToken())};
        spDownload->AppendHeader(authHeader);

        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDREAM) +
                          "/" + uuid + "/url";

        if (spDownload->Perform(url)) {
            try {
                boost::property_tree::ptree pt;
                std::istringstream is(spDownload->Data());
                boost::property_tree::read_json(is, pt);

                bool success = pt.get<bool>("success", false);
                if (success) {
                    return pt.get<std::string>("data.url", "");
                } else {
                    g_Log->Error("JSON response indicates failure");
                    return "";
                }
            } catch (const boost::property_tree::json_parser_error& e) {
                g_Log->Error("Failed to parse JSON response: %s", e.what());
                return "";
            }
        } else {
            if (spDownload->ResponseCode() == 400 || spDownload->ResponseCode() == 401) {
                if (currentAttempt == maxAttempts)
                    return "";
                if (!RefreshAccessToken())
                    return "";
            } else {
                g_Log->Error("Failed to get dream download link. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    return "";
}


std::vector<std::string> EDreamClient::ParsePlaylist(std::string_view uuid) {
    g_Log->Info("Parse Playlist %s", (uuid == "" ? "default playlist" : uuid));
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    // Collect all UUIDs from the json, and UUIDs where metadata is missing
    std::vector<std::string> uuids;
    std::vector<std::string> needsMetadataUuids;
    std::vector<std::string> needsDownloadUuids;

    // Open playlist and grab content. Default playlist is named playlist_0
    fs::path filePath = Cache::PathManager::getInstance().jsonPlaylistPath() / (uuid == "" ? "playlist_0.json" : "playlist_" + std::string(uuid) + ".json");

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.string().c_str());
        return uuids;
    }
    std::string contents{(std::istreambuf_iterator<char>(file)),
        (std::istreambuf_iterator<char>())};
    file.close();
    

    std::string needsStreamingUuid;

    try
    {
        boost::system::error_code ec;
        auto response = json::parse(contents, ec).as_object();
        auto data = response["data"].as_object();
        
        // Urgggghh, default playlist doesn't use the same format...
        boost::json::array itemArray;
        if (uuid == "") {
            itemArray = data["playlist"].as_array();
        } else {
            auto playlist = data["playlist"].as_object();
            itemArray = playlist["contents"].as_array();
        }

        bool isFirst = true;
        
        for (auto& item : itemArray) {
            auto itemObj = item.as_object();
            
            auto uuid = std::string(itemObj["uuid"].as_string());
            auto timestamp = itemObj["timestamp"].as_int64();
            
            // Do we have the metadata?
            if (cm.needsMetadata(uuid, timestamp)) {
                needsMetadataUuids.push_back(uuid.c_str());
            }
            
            // Do we have the video?
            if (!cm.hasDiskCachedItem(uuid.c_str()))
            {
                if (!isFirst) {
                    needsDownloadUuids.push_back(uuid.c_str());
                } else {
                    // Prefetch download link for 1st video from playlist if we don't have it
                    // We don't push it to our download thread
                    needsStreamingUuid = uuid;
                }
            }
            
            uuids.push_back(uuid.c_str());
            isFirst = false;
        }
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, contents);
    }

    /*
    // Now we fetch metadata that needs fetching, if any
    // We transform our vector into a comma separated string to be fetched
    if (!needsMetadataUuids.empty()) {
        std::ostringstream oss;
        
        // Copy all elements except the last one, followed by the delimiter
        std::copy(needsMetadataUuids.begin(), needsMetadataUuids.end() - 1,
                  std::ostream_iterator<std::string>(oss, ","));
        
        // Add the last element without the delimiter
        oss << needsMetadataUuids.back();

        g_Log->Info("Needs metadata for : %s",oss.str().c_str());
    }
    */
    // TMP we fetch each individually, we'll move to multiple with the code above.
    // This will require a Fetch that can split the results in multiple files though TODO
    for (const auto& needsMetadata : needsMetadataUuids) {
        FetchDreamMetadata(needsMetadata);
        cm.reloadMetadata(needsMetadata);
    }

    // Then enqueue our missing videos
    for (const auto& needsDownload : needsDownloadUuids) {
        if (!g_ContentDownloader().m_gDownloader.isDreamUUIDQueued(needsDownload)) {
            g_Log->Info("Add %s to download queue", needsDownload.c_str());
            g_ContentDownloader().m_gDownloader.AddDreamUUID(needsDownload);
        }
    }

    // Finally, if needed fetch streaming link for 1st video
    if (!needsStreamingUuid.empty()) {
        // Grab a pointer to the dream metadata
        auto dream = cm.getDream(needsStreamingUuid);

        // Grab streaming URL and save it for later use
        auto path = EDreamClient::GetDreamDownloadLink(dream->uuid);
        dream->setStreamingUrl(path);
    }
    
    return uuids;
}

std::tuple<std::string, std::string, bool, int64_t> EDreamClient::ParsePlaylistMetadata(std::string_view uuid) {
    // Default playlist doesn't have any metadata right now, hardcoding this
    if (uuid.empty()) {
        return {"Popular dreams", "Various artists", false, 0};
    }
    
    // Open playlist and grab content
    fs::path filePath = Cache::PathManager::getInstance().jsonPlaylistPath() / ("playlist_" + std::string(uuid) + ".json");
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.string().c_str());
        return {"", "", false, 0};
    }
    std::string contents{(std::istreambuf_iterator<char>(file)),
        (std::istreambuf_iterator<char>())};
    file.close();
    
    try
    {
        boost::property_tree::ptree pt;
        std::istringstream is(contents);
        boost::property_tree::read_json(is, pt);

        auto data = pt.get_child("data");
        auto playlist = data.get_child("playlist");
        
        std::string name = playlist.get<std::string>("name", "");
        std::string artist = playlist.get<std::string>("artist", "");
        bool nsfw = playlist.get<bool>("nsfw", false);
        int64_t timestamp = playlist.get<int64_t>("timestamp", 0);

        return {name, artist, nsfw, timestamp};
    }
    catch (const boost::property_tree::json_parser_error& e)
    {
        g_Log->Error("JSON parsing error: %s", e.what());
    }
    catch (const std::exception& e)
    {
        g_Log->Error("Error parsing playlist metadata: %s", e.what());
    }
    
    return {"", "", false, 0};
}


bool EDreamClient::EnqueuePlaylist(std::string_view uuid) {
    // Fetch the playlist and save it to disk

    // First, fetch the playlist asynchronously
    auto fetchFuture = FetchPlaylistAsync(std::string(uuid));
    
    // Wait for the fetch to complete
    bool fetchSuccess = fetchFuture.get();
    
    if (!fetchSuccess) {
        g_Log->Error("Failed to fetch playlist. UUID: %s", std::string(uuid).c_str());
        return false;
    }

    // Parse the playlist, we do this here as, in cascade, it will also fetch any dream metadata we need
    auto uuids = ParsePlaylist(uuid);
    
    if (uuids.empty()) {
        g_Log->Error("Failed to parse playlist or playlist is empty. UUID: %s", std::string(uuid).c_str());
        return false;
    }

    // save the current playlist id, this will get reused at next startup
    g_Settings()->Set("settings.content.current_playlist_uuid", uuid);
    g_Player().SetPlaylist(std::string(uuid));
    
    g_Player().SetTransitionDuration(1.0f);
    g_Player().StartTransition();
    
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
        
        int64_t frameNumber = -1;
        if (response.find("frameNumber") != response.end()) {
            std::shared_ptr<sio::int_message> frameNumberObj =
                        std::dynamic_pointer_cast<sio::int_message>(response["frameNumber"]);
            frameNumber = frameNumberObj->get_int();
            printf("Frame number: %" PRId64, frameNumber);
        }
        
        printf("should play : %s", uuid.data());
        g_Player().PlayDreamNow(uuid.data(), frameNumber);
    } else if (event == "play_playlist") {
        std::shared_ptr<sio::string_message> uuidObj =
            std::dynamic_pointer_cast<sio::string_message>(response["uuid"]);
        std::string_view uuid = uuidObj->get_string();
        printf("should play : %s", uuid.data());
        
        EDreamClient::EnqueuePlaylistAsync(uuid.data());
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

void EDreamClient::SendPlayingDream(std::string uuid) 
{
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
    s_SIOClient.connect(ServerConfig::ServerConfigManager::getInstance().getWebsocketServer(), query);
    
    // Send first ping immediately so frontend knows we're here
    SendPing();

    // Run the io_context in a separate thread
    std::thread([&]() {
        io_context->run();
    }).detach();
}

void EDreamClient::Like(std::string uuid) {
    std::cout << "Sending like for UUID " << uuid;
    
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "like");
    ms->insert("uuid", uuid);
    sio::message::list list;
    list.push(ms);
    s_SIOClient.socket("/remote-control")
        ->emit("new_remote_control_event", list);
}

void EDreamClient::Dislike(std::string uuid) {
    std::cout << "Sending dislike for UUID " << uuid;
    
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "dislike");
    ms->insert("uuid", uuid);
    sio::message::list list;
    list.push(ms);
    s_SIOClient.socket("/remote-control")
        ->emit("new_remote_control_event", list);
}

void EDreamClient::SetCPUUsage(int _cpuUsage) { fCpuUsage.exchange(_cpuUsage); }

