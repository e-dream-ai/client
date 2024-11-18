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
#include "Settings.h"
#include "ServerConfig.h"
#include "NetworkConfig.h"
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
    if (g_Client()->IsMultipleInstancesMode()) {
        g_Log->Info("Disabling auth in multiple instance mode");
        return;
    }

    s_SIOClient.set_open_listener(&OnWebSocketConnected);
    s_SIOClient.set_close_listener(&OnWebSocketClosed);
    s_SIOClient.set_fail_listener(&OnWebSocketFail);
    s_SIOClient.set_reconnecting_listener(&OnWebSocketReconnecting);
    s_SIOClient.set_reconnect_listener(&OnWebSocketReconnect);

    fAuthMutex.lock();
    boost::thread authThread(&EDreamClient::Authenticate);
}

void EDreamClient::DeinitializeClient()
{
    // Multiple instances do not connect to websocket, just return
    if (g_Client()->IsMultipleInstancesMode()) {
        return;
    }
    // Stop the timer and io_context
    ping_timer->cancel();
    io_context->stop();

    // Send goodbye message
    SendGoodbye();

    // WARNING, enabling this causes socket.io to crash. There's a workaround but
    // it's messy, see here
    // https://github.com/socketio/socket.io-client-cpp/issues/404
    // IF we fix this, we can reenable connection closing and enable account switwching
    // Close the WebSocket connection
    //s_SIOClient.close();
  
    /*
    s_SIOClient.set_open_listener(nullptr);
    s_SIOClient.set_close_listener(nullptr);
    s_SIOClient.set_fail_listener(nullptr);
    s_SIOClient.set_reconnecting_listener(nullptr);
    s_SIOClient.set_reconnect_listener(nullptr);*/
}

// MARK : Auth (via refresh)
bool EDreamClient::Authenticate()
{
    PlatformUtils::SetThreadName("Authenticate");
    g_Log->Info("Starting Authentication...");
    //std::lock_guard<std::mutex> lock(fAuthMutex);
    
    // Check if we have a sealed session
    std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));

    bool success = false;
    if (!sealedSession.empty())
    {
        // Attempt to refresh the sealed session
        if (RefreshSealedSession())
        {
            g_Log->Info("Successfully refreshed sealed session");
            success = true;
        }
        else
        {
            g_Log->Warning("Failed to refresh sealed session");
        }
    }
    else
    {
        g_Log->Warning("No sealed session found");
    }

    fIsLoggedIn.exchange(success);
    fAuthMutex.unlock();
    g_Log->Info("Sign in success: %s", success ? "true" : "false");

    if (success)
    {
        boost::thread webSocketThread(
            &EDreamClient::ConnectRemoteControlSocket);
    } else {
        // Clear the sealed session as it's no longer valid
        g_Settings()->Set("settings.content.sealed_session", std::string(""));
        g_Settings()->Storage()->Commit();
    }

    return success;
}

// MARK: - Sign-in/out status, used externally
void EDreamClient::DidSignIn()
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    fIsLoggedIn.exchange(true);
    boost::thread webSocketThread(&EDreamClient::ConnectRemoteControlSocket);
}

void EDreamClient::SignOut()
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    
    g_Log->Info("Sign out initiated");
    
    // Retrieve the current sealed session from settings
    std::string currentSealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
    
    if (currentSealedSession.empty())
    {
        g_Log->Error("No current sealed session found in settings");
        return;
    }

    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("Sign out Sealed Session");
    Network::NetworkHeaders::addStandardHeaders(spDownload);
    spDownload->AppendHeader("Content-Type: application/json");
    
    // Set the cookie with the current sealed session
    std::string cookieHeader = "Cookie: wos-session=" + currentSealedSession;
    spDownload->AppendHeader(cookieHeader);

    if (spDownload->Perform(ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGOUT)))
    {
        if (spDownload->ResponseCode() == 200)
        {
            g_Log->Info("Logged out successful");
        }
        else
        {
            g_Log->Error("Failed to sign out. Server returned %i: %s",
                         spDownload->ResponseCode(), spDownload->Data().c_str());
        }
    }
    else
    {
        g_Log->Error("Network error while signing out");
    }
    
    fIsLoggedIn.exchange(false);
    g_Settings()->Set("settings.content.sealed_session", std::string(""));
    g_Settings()->Set("settings.content.refresh_token", std::string(""));
    g_Settings()->Storage()->Commit();

    // Shutdown websocket
    DeinitializeClient();
    
    g_Player().Stop();
}

bool EDreamClient::IsLoggedIn()
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    return fIsLoggedIn.load();
}


// MARK: - Auth v2
// Callback function to write response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool EDreamClient::SendCode()
{
    std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
    
    if (email.empty())
    {
        g_Log->Error("Email address not found in settings");
        return false;
    }

    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC);
        
        // Prepare JSON payload
        boost::json::object payload;
        payload["email"] = email;
        std::string jsonBody = boost::json::serialize(payload);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if(res != CURLE_OK) {
            g_Log->Error("Failed to send verification code. Curl error: %s", curl_easy_strerror(res));
            return false;
        }

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200) {
            g_Log->Info("Verification code sent successfully to %s", email.c_str());
            return true;
        } else {
            g_Log->Error("Failed to send verification code. Server returned %ld: %s", http_code, readBuffer.c_str());
            return false;
        }
    }

    g_Log->Error("Failed to initialize curl");
    return false;
}

bool EDreamClient::ValidateCode(const std::string& code)
{
    std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
    
    if (email.empty())
    {
        g_Log->Error("Email address not found in settings");
        return false;
    }

    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC);
        
        // Prepare JSON payload
        boost::json::object payload;
        payload["email"] = email;
        payload["code"] = code;
        std::string jsonBody = boost::json::serialize(payload);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if(res != CURLE_OK) {
            g_Log->Error("Failed to validate code. Curl error: %s", curl_easy_strerror(res));
            return false;
        }

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200) {
            try {
                boost::json::value response = boost::json::parse(readBuffer);
                boost::json::object responseObj = response.as_object();
                
                if (!responseObj.contains("success") || !responseObj["success"].as_bool()) {
                    g_Log->Error("Validation failed: %s", responseObj.contains("message") ? responseObj["message"].as_string().c_str() : "Unknown error");
                    return false;
                }
                
                if (!responseObj.contains("data") || !responseObj["data"].is_object()) {
                    g_Log->Error("Response doesn't contain data object");
                    return false;
                }
                
                boost::json::object dataObj = responseObj["data"].as_object();
                
                if (dataObj.contains("sealedSession") && dataObj["sealedSession"].is_string()) {
                    std::string sealedSession = dataObj["sealedSession"].as_string().c_str();
                    g_Settings()->Set("settings.content.sealed_session", sealedSession);
                    g_Settings()->Storage()->Commit();
                    
                    g_Log->Info("Sealed session saved successfully");
                    //g_Player().Start();
                } else {
                    g_Log->Error("sealedSession not found in the response data");
                    return false;
                }
                
                if (dataObj.contains("user") && dataObj["user"].is_object()) {
                    boost::json::object userObj = dataObj["user"].as_object();
                    g_Log->Info("User object received: %s", boost::json::serialize(userObj).c_str());
                } else {
                    g_Log->Warning("User object not found in the response data");
                }
                
                return true;
            } catch (const boost::json::system_error& e) {
                g_Log->Error("JSON parsing error: %s", e.what());
                return false;
            }
        } else {
            g_Log->Error("Failed to validate code. Server returned %ld: %s", http_code, readBuffer.c_str());
            return false;
        }
    }

    g_Log->Error("Failed to initialize curl");
    return false;
}

bool EDreamClient::RefreshSealedSession()
{
    // Retrieve the current sealed session from settings
    std::string currentSealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
    
    if (currentSealedSession.empty())
    {
        g_Log->Error("No current sealed session found in settings");
        return false;
    }

    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("Refresh Sealed Session");
    Network::NetworkHeaders::addStandardHeaders(spDownload);
    spDownload->AppendHeader("Content-Type: application/json");
    
    // Set the cookie with the current sealed session
    std::string cookieHeader = "Cookie: wos-session=" + currentSealedSession;
    spDownload->AppendHeader(cookieHeader);

    // Prepare an empty POST body
    const char* emptyBody = "{}";
    spDownload->SetPostFields(emptyBody);

    if (spDownload->Perform(ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_REFRESH)))
    {
        if (spDownload->ResponseCode() == 200)
        {
            try
            {
                boost::json::value response = boost::json::parse(spDownload->Data());
                boost::json::object responseObj = response.as_object();

                // Check if the response was successful
                if (!responseObj.contains("success") || !responseObj["success"].as_bool())
                {
                    g_Log->Error("Sealed session refresh failed: %s", responseObj.contains("message") ? responseObj["message"].as_string().c_str() : "Unknown error");
                    return false;
                }

                // Check for the data object
                if (!responseObj.contains("data") || !responseObj["data"].is_object())
                {
                    g_Log->Error("Response doesn't contain data object");
                    return false;
                }

                boost::json::object dataObj = responseObj["data"].as_object();

                // Retrieve and save the new sealedSession
                if (dataObj.contains("sealedSession") && dataObj["sealedSession"].is_string())
                {
                    std::string newSealedSession = dataObj["sealedSession"].as_string().c_str();
                    g_Settings()->Set("settings.content.sealed_session", newSealedSession);
                    g_Settings()->Storage()->Commit();  // Save the settings

                    g_Log->Info("New sealed session saved successfully");
                    return true;
                }
                else
                {
                    g_Log->Error("New sealedSession not found in the response data");
                    return false;
                }
            }
            catch (const boost::system::system_error& e)
            {
                g_Log->Error("JSON parsing error: %s", e.what());
                return false;
            }
        }
        else
        {
            g_Log->Error("Failed to refresh sealed session. Server returned %i: %s",
                         spDownload->ResponseCode(), spDownload->Data().c_str());
            return false;
        }
    }
    else
    {
        g_Log->Error("Network error while refreshing sealed session");
        return false;
    }
}

void EDreamClient::ParseAndSaveCookies(const Network::spCFileDownloader& spDownload) {
    std::vector<std::string> setCookieHeaders = spDownload->GetResponseHeaders("set-cookie");

    for (const auto& setCookieHeader : setCookieHeaders) {
        size_t pos = setCookieHeader.find('=');
        if (pos != std::string::npos) {
            std::string name = setCookieHeader.substr(0, pos);
            std::string value = setCookieHeader.substr(pos + 1);
            
            // Remove any additional attributes after the value
            pos = value.find(';');
            if (pos != std::string::npos) {
                value = value.substr(0, pos);
            }
            
            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (name == "wos-session") {
                g_Settings()->Set("settings.content.sealed_session", value);
                g_Log->Info("Updated wos-session cookie");
            } else if (name == "connect.sid") {
                g_Settings()->Set("settings.content.connect_sid", value);
                g_Log->Info("Updated connect.sid cookie");
            } else {
                g_Log->Info("Other cookie : %s", name.c_str());
            }
        }
    }

    if (!setCookieHeaders.empty()) {
        g_Settings()->Storage()->Commit();
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
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return "";
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
       
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
                if (!RefreshSealedSession())
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
    
    ParseAndSaveCookies(spDownload);
    
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
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return dislikes;
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
        
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDISLIKES);
        
        if (spDownload->Perform(url))
        {
            ParseAndSaveCookies(spDownload);
            
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
                if (!RefreshSealedSession())
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
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return "";
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
        
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
                if (!RefreshSealedSession())
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

    ParseAndSaveCookies(spDownload);
    
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
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return "";
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
        
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
                if (!RefreshSealedSession())
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

    ParseAndSaveCookies(spDownload);
    
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
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return false;
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
        
        
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
                if (!RefreshSealedSession())
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

    ParseAndSaveCookies(spDownload);
    
    auto filename = jsonPath / (uuid + ".json");
    if (!spDownload->Save(filename.string()))
    {
        g_Log->Error("Unable to save %s\n", filename.string().c_str());
        return false;
    }
    
    return true;
}

bool EDreamClient::FetchDreamsMetadata(const std::vector<std::string>& uuids) {
    if (uuids.empty()) {
        return false;
    }
    
    auto jsonPath = Cache::PathManager::getInstance().jsonDreamPath();
    Network::spCFileDownloader spDownload;
    int maxAttempts = 3;
    int currentAttempt = 0;
        
    while (currentAttempt++ < maxAttempts) {
        spDownload = std::make_shared<Network::CFileDownloader>("Metadata");
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return false;
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
        
        // Create request body
        boost::json::object requestBody;
        boost::json::array uuidArray;
        for (const auto& uuid : uuids) {
            uuidArray.emplace_back(boost::json::string(uuid));
        }
        requestBody["uuids"] = uuidArray;
        std::string jsonBody = boost::json::serialize(requestBody);
        spDownload->SetPostFields(jsonBody.c_str());
       
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDREAM);
        g_Log->Info("Fetching metadata for %zu dreams", uuids.size());
               
        if (spDownload->Perform(url)) {
            break;
        } else {
            if (spDownload->ResponseCode() == 400 ||
                spDownload->ResponseCode() == 401) {
                if (currentAttempt == maxAttempts)
                    return false;
                if (!RefreshSealedSession())
                    return false;
            } else {
                g_Log->Error("Failed to get metadata. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    ParseAndSaveCookies(spDownload);
        
    try {
        boost::json::value response = boost::json::parse(spDownload->Data());
        auto dreams = response.as_object()["data"].as_object()["dreams"].as_array();
        
        for (const auto& dream : dreams) {
            // Create individual dream response
            boost::json::object individual_response;
            individual_response["success"] = true;
            
            boost::json::object data;
            boost::json::array dreams_array;
            dreams_array.push_back(dream);
            data["dreams"] = dreams_array;
            individual_response["data"] = data;
            
            // Get UUID for filename
            const auto& dream_obj = dream.as_object();
            std::string uuid = dream_obj.at("uuid").as_string().c_str();
            
            // Save to file
            auto filename = jsonPath / (uuid + ".json");
            std::ofstream file(filename);
            if (file.is_open()) {
                file << boost::json::serialize(individual_response);
                file.close();
                g_Log->Info("Saved metadata for dream: %s", uuid.c_str());
            } else {
                g_Log->Error("Unable to save metadata file for %s", uuid.c_str());
            }
        }
    } catch (const boost::system::system_error& e) {
        JSONUtil::LogException(e, spDownload->Data());
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
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            return "";
        }
        
        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);

        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::GETDREAM) +
                          "/" + uuid + "/url";

        if (spDownload->Perform(url)) {
            ParseAndSaveCookies(spDownload);
            
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
                if (!RefreshSealedSession())
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

    // Do we need to fetch metadata?
    if (!needsMetadataUuids.empty()) {
        // send that array and try to fetch all at once
        FetchDreamsMetadata(needsMetadataUuids);

        // Then reload metadata for each of the uuids
        for (const auto& needsMetadata : needsMetadataUuids) {
            cm.reloadMetadata(needsMetadata);
        }
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
    else if (event == "like_current_dream")
    {
        g_Client()->ExecuteCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_LIKE);
    }
    else if (event == "dislike_current_dream")
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
    
    std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
    
    query["Cookie"] = string_format("wos-session=%s", sealedSession.c_str());
    query["Edream-Client-Type"] = PlatformUtils::GetPlatformName();
    query["Edream-Client-Version"] = PlatformUtils::GetAppVersion();

    s_SIOClient.connect(ServerConfig::ServerConfigManager::getInstance().getWebsocketServer(), query, query);
    
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

