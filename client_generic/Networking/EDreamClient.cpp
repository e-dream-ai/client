/*#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>*/
#include <sio_client.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <cstdio>
#include <future>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>

#include "ContentDownloader.h"
#include "StringFormat.h"
#include "Log.h"
#include "Player.h"
#include "Settings.h"
#include "ServerConfig.h"
#include "NetworkConfig.h"
#include "PathManager.h"
#include "client.h"
#include "Hud.h"
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

static ShowFirstTimeSetupCallback_t gShowFirstTimeSetupCallback = nullptr;
void ESSetShowFirstTimeSetupCallback(ShowFirstTimeSetupCallback_t _callback)
{
    gShowFirstTimeSetupCallback = _callback;
}
void ESShowFirstTimeSetup()
{
    if (gShowFirstTimeSetupCallback != nullptr)
    {
        gShowFirstTimeSetupCallback();
    }
}

long long EDreamClient::remainingQuota = 0;
std::chrono::system_clock::time_point EDreamClient::quotaExpiresAt = std::chrono::system_clock::now();

std::atomic<bool> EDreamClient::fIsLoggedIn(false);
std::atomic<bool> EDreamClient::fAuthRetryAbort(false);
std::atomic<bool> EDreamClient::fAuthRetryPending(false);
std::atomic<bool> EDreamClient::fInitialAuthComplete(false);
std::atomic<int> EDreamClient::fCpuUsage(0);
std::mutex EDreamClient::fAuthMutex;
std::condition_variable EDreamClient::fAuthCV;
std::mutex EDreamClient::fWebSocketMutex;
std::atomic<bool> EDreamClient::fIsWebSocketConnected(false);
std::atomic<int> fWebSocketConnectionAttempts(0);

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


std::unique_ptr<boost::asio::io_context> EDreamClient::io_context = nullptr;
std::unique_ptr<boost::asio::steady_timer> EDreamClient::ping_timer = nullptr;
std::unique_ptr<boost::asio::steady_timer> EDreamClient::quota_timer = nullptr;

static void OnQuotaUpdate(sio::event& _wsEvent);

// MARK: Ping via websocket
void EDreamClient::SendPing()
{
    // Check if websocket is connected
    auto socket = s_SIOClient.socket("/remote-control");
    if (!socket) {
        g_Log->Warning("SendPing: WebSocket not connected, skipping ping");
        ScheduleNextPing();
        return;
    }

    // If socket is available but callbacks aren't bound yet, bind them now
    // This handles the race condition where namespace becomes ready after OnWebSocketConnected
    if (!fIsWebSocketConnected.load()) {
        g_Log->Info("SendPing: Socket available but not marked connected, binding callbacks now");
        socket->off("new_remote_control_event");
        socket->on("new_remote_control_event", &OnWebSocketMessage);
        socket->off("quota_update");
        socket->on("quota_update", &OnQuotaUpdate);
        fIsWebSocketConnected.exchange(true);
    }

    // Send simple ping first (for backwards compatibility / basic keepalive)
    socket->emit("ping");
    g_Log->Info("WebSocket emit: event='ping'");

    // Get current player state
    const ContentDecoder::sClipMetadata* clipMetadata = g_Player().GetCurrentPlayingClipMetadata();
    const ContentDecoder::sFrameMetadata* frameMetadata = g_Player().GetCurrentFrameMetadata();

    // Create state sync message
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());

    // Add current dream UUID if available
    std::string dreamUUID = "none";
    if (clipMetadata && !clipMetadata->dreamData.uuid.empty()) {
        dreamUUID = clipMetadata->dreamData.uuid;
        ms->insert("dream_uuid", dreamUUID);
    }

    // Add current playlist UUID if available
    std::string playlistUUID = g_Settings()->Get("settings.content.current_playlist_uuid", std::string(""));
    if (!playlistUUID.empty()) {
        ms->insert("playlist", playlistUUID);
    }

    // Add current timecode using the same calculation as the HUD display
    // This ensures timecode in state_sync matches what's shown on screen
    double timecode = 0.0;
    if (frameMetadata && clipMetadata) {
        double baseFps = std::stod(clipMetadata->dreamData.fps);
        auto [currentTime, totalTime] = CElectricSheep::CalculateTimecode(frameMetadata, baseFps);
        timecode = currentTime;
    }
    if (timecode < 0.0) timecode = 0.0;  // Clamp to 0 if negative
    ms->insert("timecode", std::to_string(timecode));

    // Add HUD state
    std::string hudState = "none";
    Hud::spCHudManager hudManager = g_Client()->GetHudManager();
    if (hudManager) {
        auto helpEntry = hudManager->Get("helpmessage");
        auto statsEntry = hudManager->Get("dreamstats");

        if (helpEntry && helpEntry->Visible()) {
            hudState = "help";
        } else if (statsEntry && statsEntry->Visible()) {
            hudState = "stats";
        }
        ms->insert("hud", hudState);
    }

    // Add paused state
    std::string pausedState = g_Player().IsPaused() ? "true" : "false";
    ms->insert("paused", pausedState);

    // Add playback speed (FPS)
    double playbackSpeed = g_Player().GetPerceptualFPS();
    ms->insert("playback_speed", std::to_string(playbackSpeed));

    // Send state sync data
    sio::message::list list;
    list.push(ms);
    socket->emit("state_sync", list);

    // Log EXACTLY what was sent to socket
    g_Log->Info("WebSocket emit: event='state_sync' data={dream_uuid:'%s', playlist:'%s', timecode:%g, hud:'%s', paused:'%s', playback_speed:%g}",
                dreamUUID.c_str(),
                !playlistUUID.empty() ? playlistUUID.c_str() : "none",
                timecode,
                hudState.c_str(),
                pausedState.c_str(),
                playbackSpeed);

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

void EDreamClient::SendStateUpdate()
{
    // Post to io_context thread for thread safety
    // boost::asio timers are not thread-safe for cancel() from multiple threads
    if (io_context && !io_context->stopped()) {
        boost::asio::post(*io_context, []() {
            ping_timer->cancel();
            SendPing();
        });
    }
}

// MARK: Quota update timer
void EDreamClient::UpdateQuota()
{
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    Network::spCFileDownloader spDownload;

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = std::make_shared<Network::CFileDownloader>("UpdateQuota");
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");

        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));

        if (sealedSession.empty()) {
            g_Log->Error("UpdateQuota: Sealed session not found in settings");
            ScheduleNextQuotaUpdate();
            return;
        }

        // Set the cookie with the sealed session
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);

        std::string url{ ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::QUOTA) };

        if (spDownload->Perform(url))
        {
            break;
        }
        else
        {
            if (spDownload->ResponseCode() == 400 ||
                spDownload->ResponseCode() == 401)
            {
                if (currentAttempt == maxAttempts) {
                    g_Log->Error("UpdateQuota: Failed after %d attempts", maxAttempts);
                    ScheduleNextQuotaUpdate();
                    return;
                }
                if (RefreshSealedSession() != AuthRefreshResult::Success) {
                    g_Log->Error("UpdateQuota: Failed to refresh session");
                    ScheduleNextQuotaUpdate();
                    return;
                }
            }
            else
            {
                g_Log->Error("UpdateQuota: Failed to get quota. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
            }
        }
    }

    // Parse the quota response
    try
    {
        json::value response = json::parse(spDownload->Data());
        json::value data = response.at("data");
        json::value quota = data.at("quota");

        remainingQuota = quota.as_int64();

        // Update CacheManager with the new quota
        cm.setRemainingQuota(remainingQuota);

        // Parse quotaExpiresAt if present
        if (data.as_object().if_contains("quotaExpiresAt")) {
            json::value quotaExpiresAtValue = data.at("quotaExpiresAt");
            std::string expiresAtStr = quotaExpiresAtValue.as_string().c_str();
            
            // Parse ISO 8601 datetime string (e.g., "2026-02-27T16:37:09.942Z")
            std::tm tm = {};
            std::istringstream ss(expiresAtStr);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            
            if (!ss.fail()) {
                auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));
                quotaExpiresAt = tp;
                cm.setQuotaExpiresAt(tp);

                g_Log->Info("UpdateQuota: Successfully updated quota to %lld, expires at %s",
                           remainingQuota, expiresAtStr.c_str());
            } else {
                g_Log->Warning("UpdateQuota: Failed to parse quotaExpiresAt: %s", expiresAtStr.c_str());
                g_Log->Info("UpdateQuota: Successfully updated quota to %lld", remainingQuota);
            }
        } else {
            g_Log->Info("UpdateQuota: Successfully updated quota to %lld", remainingQuota);
        }
    }
    catch (const boost::system::system_error& e)
    {
        g_Log->Error("UpdateQuota: Failed to parse quota response");
        JSONUtil::LogException(e, spDownload->Data());
    }

    // Schedule the next update
    ScheduleNextQuotaUpdate();
}

void EDreamClient::ScheduleNextQuotaUpdate()
{
    g_Log->Info("Scheduling next quota update in one hour");
    // Schedule quota update every hour (3600 seconds)
    quota_timer->expires_after(std::chrono::seconds(3600));
    quota_timer->async_wait([](const boost::system::error_code& ec) {
        if (!ec) {
            UpdateQuota();
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

    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        socket->emit("goodbye");
        g_Log->Info("Goodbye message sent");
    }
}

static void BindWebSocketCallbacks()
{
    g_Log->Info("Binding web socket callbacks");
    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        g_Log->Info("WebSocket socket found, binding callbacks");
        socket->off("new_remote_control_event");
        socket->on("new_remote_control_event", &OnWebSocketMessage);
        socket->off("quota_update");
        socket->on("quota_update", &OnQuotaUpdate);
        EDreamClient::fIsWebSocketConnected.exchange(true);
    } else {
        // Namespace socket not ready yet - retry after a short delay
        g_Log->Warning("WebSocket namespace not ready, scheduling retry...");
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            auto socket = s_SIOClient.socket("/remote-control");
            if (socket) {
                g_Log->Info("WebSocket socket found on retry, binding callbacks");
                socket->off("new_remote_control_event");
                socket->on("new_remote_control_event", &OnWebSocketMessage);
                socket->off("quota_update");
                socket->on("quota_update", &OnQuotaUpdate);
                EDreamClient::fIsWebSocketConnected.exchange(true);
            } else {
                g_Log->Error("WebSocket namespace still not available after retry");
            }
        }).detach();
    }
}

void EDreamClient::UnbindWebSocketCallbacks()
{
    fIsWebSocketConnected.exchange(false);
    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        socket->off("new_remote_control_event");
        socket->off("quota_update");
    }
}

bool EDreamClient::IsWebSocketConnected()
{
    return s_SIOClient.opened() && fIsWebSocketConnected;
}

static void OnWebSocketConnected()
{
    g_Log->Info("WebSocket connected successfully.");

    // Bind callbacks only after connection is established
    BindWebSocketCallbacks();

    // Reset the connection attempt count
    fWebSocketConnectionAttempts = 0;
}

static void OnWebSocketClosed(const sio::client::close_reason& _reason)
{
    g_Log->Info("WebSocket connection closed. Reason: %d", static_cast<int>(_reason));
    EDreamClient::UnbindWebSocketCallbacks();
}

static void OnWebSocketFail()
{
    g_Log->Error("WebSocket connection failed.");
    EDreamClient::UnbindWebSocketCallbacks();
    fWebSocketConnectionAttempts++;
}

static void OnWebSocketReconnecting()
{
    g_Log->Info("WebSocket reconnecting...");
    EDreamClient::UnbindWebSocketCallbacks();
}

static void OnWebSocketReconnect(unsigned _num, unsigned _delay)
{
    g_Log->Info("WebSocket reconnected after %u attempts, delay was %u ms", _num, _delay);
    BindWebSocketCallbacks();
}

void EDreamClient::InitializeClient()
{
    if (g_Client()->IsMultipleInstancesMode()) {
        g_Log->Info("Disabling auth in multiple instance mode");
        return;
    }

    // Initialize io_context and timers on first use to avoid static initialization order issues
    if (!io_context) {
        io_context = std::make_unique<boost::asio::io_context>();
        ping_timer = std::make_unique<boost::asio::steady_timer>(*io_context);
        quota_timer = std::make_unique<boost::asio::steady_timer>(*io_context);
    }

    // Start in offline mode until initial auth completes; this prevents
    // remote/online UI from appearing before we know the auth result.
    g_Player().SetOfflineMode(true);

    s_SIOClient.set_open_listener(&OnWebSocketConnected);
    s_SIOClient.set_close_listener(&OnWebSocketClosed);
    s_SIOClient.set_fail_listener(&OnWebSocketFail);
    s_SIOClient.set_reconnecting_listener(&OnWebSocketReconnecting);
    s_SIOClient.set_reconnect_listener(&OnWebSocketReconnect);

    fInitialAuthComplete.store(false);

    boost::thread authThread(&EDreamClient::Authenticate);
    authThread.detach();
}

void EDreamClient::DeinitializeClient()
{
    // Signal auth retry loop to exit (if running)
    fAuthRetryAbort.store(true);
    // Multiple instances do not connect to websocket, just return
    if (g_Client()->IsMultipleInstancesMode()) {
        return;
    }
    // Mark websocket as disconnected immediately
    UnbindWebSocketCallbacks();
    // Stop the timers and io_context
    if (ping_timer) {
        ping_timer->cancel();
    }
    if (quota_timer) {
        quota_timer->cancel();
    }
    if (io_context) {
        io_context->stop();
    }

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
    fAuthRetryAbort.store(false);
    fAuthRetryPending.store(false);
    g_Log->Info("Starting Authentication...");

    // Check if we have a sealed session
    std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));

    if (sealedSession.empty())
    {
        g_Log->Warning("No sealed session found");
        fIsLoggedIn.exchange(false);
        fInitialAuthComplete.store(true);
        fAuthCV.notify_one();
        if (!shownSettingsOnce) {
            shownSettingsOnce = true;
#ifdef __APPLE__
            bool firstTimeSetupCompleted = g_Settings()->Get("settings.app.firsttimesetup", false);
            if (!firstTimeSetupCompleted) {
                ESShowFirstTimeSetup();
            }
#else
            ESShowPreferences();
#endif
        }
        return false;
    }

    constexpr int kRetryDelayInitialSeconds = 60;
    constexpr int kRetryDelayMultiplier = 3;
    constexpr int kRetryDelayMaxSeconds = 24 * 3600; // 24 hours

    AuthRefreshResult result = RefreshSealedSession();

    if (result == AuthRefreshResult::Success)
    {
        fIsLoggedIn.exchange(true);
        fInitialAuthComplete.store(true);
        g_Player().SetOfflineMode(false);
        fAuthCV.notify_one();
        g_Log->Info("Sign in success: true");
        boost::thread webSocketThread(&EDreamClient::ConnectRemoteControlSocket);
        return true;
    }

    if (result == AuthRefreshResult::InvalidSession)
    {
        g_Log->Warning("Auth refresh failed: session invalid or expired");
        fIsLoggedIn.exchange(false);
        g_Settings()->Set("settings.content.sealed_session", std::string(""));
        g_Settings()->Storage()->Commit();
        fInitialAuthComplete.store(true);
        fAuthCV.notify_one();
        // Do not auto-open login/settings UI on initial auth failure; HUD will show \"Please open settings to sign in.\"
        return false;
    }

    // TransientFailure: keep sealed session, notify so startup does not block, then retry with backoff
    g_Log->Warning("Auth refresh failed (transient), will retry. Remote indicator may show until server is back.");
    fIsLoggedIn.exchange(false);
    fAuthRetryPending.store(true);  // So UI does not show "open settings to log in"
    fInitialAuthComplete.store(true);
    fAuthCV.notify_one();

    int delaySeconds = kRetryDelayInitialSeconds;
    while (!fAuthRetryAbort.load())
    {
        g_Log->Info("Auth refresh retrying in %d seconds...", delaySeconds);
        for (int remaining = delaySeconds; remaining > 0 && !fAuthRetryAbort.load(); remaining--)
        {
            boost::this_thread::sleep(boost::posix_time::seconds(1));
        }
        if (fAuthRetryAbort.load())
        {
            g_Log->Info("Auth refresh retry aborted (shutdown)");
            fAuthRetryPending.store(false);
            return false;
        }

        result = RefreshSealedSession();
        if (result == AuthRefreshResult::Success)
        {
            g_Log->Info("Auth refresh succeeded after retry");
            fAuthRetryPending.store(false);
            fIsLoggedIn.exchange(true);
            DidSignIn();
            return true;
        }
        if (result == AuthRefreshResult::InvalidSession)
        {
            g_Log->Warning("Auth refresh failed on retry: session invalid or expired");
            fAuthRetryPending.store(false);
            g_Settings()->Set("settings.content.sealed_session", std::string(""));
            g_Settings()->Storage()->Commit();
            // Do not auto-open login/settings UI here; HUD will show \"Please open settings to sign in.\"
            return false;
        }
        // TransientFailure again: increase delay (3x, cap 24h)
        delaySeconds = std::min(delaySeconds * kRetryDelayMultiplier, kRetryDelayMaxSeconds);
        g_Log->Warning("Auth refresh failed (transient), retrying in %d seconds", delaySeconds);
    }

    fAuthRetryPending.store(false);
    return false;
}

// MARK: - Sign-in/out status, used externally
void EDreamClient::DidSignIn()
{
    g_Log->Info("Did Sign-in");
    fAuthRetryAbort.store(true);  // Stop auth retry loop if it was waiting (e.g. user logged in manually)
    fAuthRetryPending.store(false);
    g_Player().SetOfflineMode(false);
    std::lock_guard<std::mutex> lock(fAuthMutex);
    fIsLoggedIn.exchange(true);

    // Restart the player if it was previously stopped (e.g., after sign-out)
    if (!g_Player().HasStarted())
    {
        g_Log->Info("Restarting player after sign-in");
        g_Player().Start();
    }
    
    // Add a small delay to allow any pending socket operations to complete
    // This helps avoid issues with the socket.io client's internal state
    boost::thread delayedWebSocketThread([]() {
        boost::this_thread::sleep(boost::posix_time::milliseconds(500));

        // Restart the io_context if it was stopped during SignOut/DeinitializeClient
        if (io_context->stopped()) {
            g_Log->Info("Restarting io_context after sign-in");
            io_context->restart();
        }

        // Start the quota update timer
        ScheduleNextQuotaUpdate();

        // Clear any existing socket state before reconnecting
        s_SIOClient.clear_con_listeners();

        // Reinitialize socket client to fix first-login connection issue
        // The socket.io client can get into an inconsistent state when listeners
        // are set during InitializeClient() but no connection is attempted
        s_SIOClient.set_open_listener(&OnWebSocketConnected);
        s_SIOClient.set_close_listener(&OnWebSocketClosed);
        s_SIOClient.set_fail_listener(&OnWebSocketFail);
        s_SIOClient.set_reconnecting_listener(&OnWebSocketReconnecting);
        s_SIOClient.set_reconnect_listener(&OnWebSocketReconnect);

        EDreamClient::ConnectRemoteControlSocket();


    });
}

void EDreamClient::SignOut()
{
    g_Log->Info("Sign out initiated");

    std::string currentSealedSession;
    {
        std::lock_guard<std::mutex> lock(fAuthMutex);
        // Retrieve the current sealed session from settings
        currentSealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));

        if (currentSealedSession.empty())
        {
            g_Log->Error("No current sealed session found in settings");
            return;
        }
    }  // Release lock before network operations

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

    {
        std::lock_guard<std::mutex> lock(fAuthMutex);
        fIsLoggedIn.exchange(false);
        g_Settings()->Set("settings.content.sealed_session", std::string(""));
        g_Settings()->Set("settings.content.refresh_token", std::string(""));
        g_Settings()->Storage()->Commit();
    }  // Release lock before external calls

    // Shutdown websocket
    DeinitializeClient();

    g_Player().Stop();
}

bool EDreamClient::IsLoggedIn()
{
    std::lock_guard<std::mutex> lock(fAuthMutex);
    return fIsLoggedIn.load();
}

bool EDreamClient::IsAuthRetryPending()
{
    return fAuthRetryPending.load();
}

bool EDreamClient::HasCompletedInitialAuth()
{
    return fInitialAuthComplete.load();
}

// MARK: - Auth v2
// Callback function to write response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

EDreamClient::AuthResult EDreamClient::SendCode() {
    std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
    
    if (email.empty())
    {
        g_Log->Error("Email address not found in settings");
        return AuthResult(false, "Email address not provided");
    }
        
    CURLcode res;
    std::string readBuffer;
        
    auto curlDeleter = [](CURL* c){ if(c) curl_easy_cleanup(c); };
    std::unique_ptr<CURL, decltype(curlDeleter)> curl(curl_easy_init(), curlDeleter);
    
    if (curl) {
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC);
        
        // Prepare JSON payload
        boost::json::object payload;
        payload["email"] = email;
        std::string jsonBody = boost::json::serialize(payload);
        
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, jsonBody.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &readBuffer);
        
        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        
        res = curl_easy_perform(curl.get());
        curl_slist_free_all(headers);
        
        if(res != CURLE_OK) {
            g_Log->Error("Failed to send verification code. Curl error: %s", curl_easy_strerror(res));
            return AuthResult(false, std::string("Error: ") + curl_easy_strerror(res));
        }
        
        long http_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code == 200) {
            g_Log->Info("Verification code sent successfully to %s", email.c_str());
            return AuthResult(true, "Verification code sent successfully");
        } else {
            g_Log->Error("Failed to send verification code. Server returned %ld: %s", http_code, readBuffer.c_str());
            
            std::string errorMessage;
            try {
                boost::json::value response = boost::json::parse(readBuffer);
                if (response.is_object() && response.as_object().contains("message")) {
                    // Extract just the error message from the JSON
                    errorMessage = response.as_object()["message"].as_string().c_str();
                } else {
                    errorMessage = readBuffer; // Use full response if not in expected format
                }
            } catch (...) {
                errorMessage = readBuffer; // Use raw response if JSON parsing fails
            }
            
            return AuthResult(false, errorMessage);
        }
    }

    return AuthResult(false, "Cannot access Network");
}

bool EDreamClient::ValidateCode(const std::string& code)
{
    return ValidateCodeDetailed(code).success;
}

EDreamClient::ValidateCodeResult EDreamClient::ValidateCodeDetailed(const std::string& code)
{
    ValidateCodeResult result{false, ValidationFailureReason::TransientFailure, 0, "Unable to validate verification code"};
    std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
    
    if (email.empty())
    {
        g_Log->Error("Email address not found in settings");
        result.reason = ValidationFailureReason::InvalidSession;
        result.message = "Email address not provided";
        return result;
    }

    CURLcode res;
    std::string readBuffer;

    auto curlDeleter = [](CURL* c){ if(c) curl_easy_cleanup(c); };
    std::unique_ptr<CURL, decltype(curlDeleter)> curl(curl_easy_init(), curlDeleter);

    if(curl) {
        std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::LOGIN_MAGIC);

        // Prepare JSON payload
        boost::json::object payload;
        payload["email"] = email;
        payload["code"] = code;
        std::string jsonBody = boost::json::serialize(payload);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, jsonBody.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &readBuffer);

        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl.get());
        curl_slist_free_all(headers);

        if(res != CURLE_OK) {
            g_Log->Error("Failed to validate code. Curl error: %s", curl_easy_strerror(res));
            result.reason = ValidationFailureReason::TransientFailure;
            result.message = std::string("Network error: ") + curl_easy_strerror(res);
            return result;
        }

        long http_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
        result.httpCode = static_cast<int>(http_code);

        if (http_code == 200) {
            try {
                boost::json::value response = boost::json::parse(readBuffer);
                boost::json::object responseObj = response.as_object();
                
                if (!responseObj.contains("success") || !responseObj["success"].as_bool()) {
                    const char* errorMessage = responseObj.contains("message")
                        ? responseObj["message"].as_string().c_str()
                        : "Unknown error";
                    g_Log->Error("Validation failed: %s", errorMessage);
                    result.reason = ValidationFailureReason::InvalidSession;
                    result.message = errorMessage;
                    return result;
                }
                
                if (!responseObj.contains("data") || !responseObj["data"].is_object()) {
                    g_Log->Error("Response doesn't contain data object");
                    result.reason = ValidationFailureReason::InvalidSession;
                    result.message = "Malformed validation response";
                    return result;
                }
                
                boost::json::object dataObj = responseObj["data"].as_object();
                
                if (dataObj.contains("sealedSession") && dataObj["sealedSession"].is_string()) {
                    std::string sealedSession = dataObj["sealedSession"].as_string().c_str();
                    g_Settings()->Set("settings.content.sealed_session", sealedSession);
                    g_Settings()->Storage()->Commit();
                    
                    g_Log->Info("Sealed session saved successfully");
                } else {
                    g_Log->Error("sealedSession not found in the response data");
                    result.reason = ValidationFailureReason::InvalidSession;
                    result.message = "No session token returned by backend";
                    return result;
                }
                
                if (dataObj.contains("user") && dataObj["user"].is_object()) {
                    boost::json::object userObj = dataObj["user"].as_object();
                    g_Log->Info("User object received: %s", boost::json::serialize(userObj).c_str());
                } else {
                    g_Log->Warning("User object not found in the response data");
                }
                
                AuthRefreshResult refreshResult = RefreshSealedSession();
                if (refreshResult == AuthRefreshResult::Success) {
                    return ValidateCodeResult{true, ValidationFailureReason::None, static_cast<int>(http_code), "Validation successful"};
                }
                if (refreshResult == AuthRefreshResult::InvalidSession) {
                    g_Log->Warning("Validation succeeded but session refresh returned invalid session");
                    result.reason = ValidationFailureReason::InvalidSession;
                    result.message = "Session token was rejected by backend";
                    return result;
                }
                g_Log->Warning("Validation succeeded but session refresh failed transiently");
                result.reason = ValidationFailureReason::TransientFailure;
                result.message = "Backend is temporarily unavailable. Please retry.";
                return result;
            } catch (const boost::system::system_error& e) {
                g_Log->Error("JSON parsing error: %s", e.what());
                result.reason = ValidationFailureReason::TransientFailure;
                result.message = "Unable to parse validation response";
                return result;
            }
        } else {
            g_Log->Error("Failed to validate code. Server returned %ld: %s", http_code, readBuffer.c_str());
            std::string errorMessage = "Validation failed";
            try {
                boost::json::value response = boost::json::parse(readBuffer);
                if (response.is_object() && response.as_object().contains("message")) {
                    errorMessage = response.as_object()["message"].as_string().c_str();
                } else if (!readBuffer.empty()) {
                    errorMessage = readBuffer;
                }
            } catch (...) {
                if (!readBuffer.empty()) {
                    errorMessage = readBuffer;
                }
            }

            if (http_code >= 400 && http_code < 500) {
                result.reason = ValidationFailureReason::InvalidSession;
            } else {
                result.reason = ValidationFailureReason::TransientFailure;
            }
            result.message = errorMessage;
            return result;
        }
    }

    g_Log->Error("Failed to initialize curl");
    result.reason = ValidationFailureReason::TransientFailure;
    result.message = "Unable to initialize network stack";
    return result;
}

EDreamClient::AuthRefreshResult EDreamClient::RefreshSealedSession()
{
    // Retrieve the current sealed session from settings
    std::string currentSealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
    
    if (currentSealedSession.empty())
    {
        g_Log->Error("No current sealed session found in settings");
        return AuthRefreshResult::InvalidSession;
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
        // Perform() returns true only for HTTP 200 (no other codes allowed in CCurlTransfer)
        int responseCode = static_cast<int>(spDownload->ResponseCode());
        if (responseCode == 200)
        {
            try
            {
                boost::json::value response = boost::json::parse(spDownload->Data());
                boost::json::object responseObj = response.as_object();

                // Check if the response was successful
                if (!responseObj.contains("success") || !responseObj["success"].as_bool())
                {
                    g_Log->Error("Sealed session refresh failed (HTTP %i): %s", responseCode,
                                 responseObj.contains("message") ? responseObj["message"].as_string().c_str() : "Unknown error");
                    return AuthRefreshResult::InvalidSession;
                }

                // Check for the data object
                if (!responseObj.contains("data") || !responseObj["data"].is_object())
                {
                    g_Log->Error("Response doesn't contain data object (HTTP %i)", responseCode);
                    return AuthRefreshResult::InvalidSession;
                }

                boost::json::object dataObj = responseObj["data"].as_object();

                // Retrieve and save the new sealedSession
                if (dataObj.contains("sealedSession") && dataObj["sealedSession"].is_string())
                {
                    std::string newSealedSession = dataObj["sealedSession"].as_string().c_str();
                    g_Settings()->Set("settings.content.sealed_session", newSealedSession);
                    g_Settings()->Storage()->Commit();  // Save the settings

                    g_Log->Info("New sealed session saved successfully");
                    return AuthRefreshResult::Success;
                }
                else
                {
                    g_Log->Error("New sealedSession not found in the response data (HTTP %i)", responseCode);
                    return AuthRefreshResult::InvalidSession;
                }
            }
            catch (const boost::system::system_error& e)
            {
                g_Log->Warning("JSON parsing error while refreshing sealed session (HTTP %i, transient), will retry: %s", responseCode, e.what());
                return AuthRefreshResult::TransientFailure;
            }
        }
        // Fallback if transfer ever allows non-200 (e.g. via Allow())
        g_Log->Warning("Auth refresh failed with HTTP %i (transient), will retry: %s",
                       responseCode, spDownload->Data().c_str());
        return AuthRefreshResult::TransientFailure;
    }
    else
    {
        int responseCode = static_cast<int>(spDownload->ResponseCode());
        if (responseCode == 0)
        {
            g_Log->Warning("Network error while refreshing sealed session (no response, transient), will retry");
            return AuthRefreshResult::TransientFailure;
        }
        if (responseCode >= 400 && responseCode < 500)
        {
            g_Log->Warning("Auth refresh rejected by server (HTTP %i): session invalid or expired: %s",
                           responseCode, spDownload->Data().c_str());
            return AuthRefreshResult::InvalidSession;
        }
        g_Log->Warning("Auth refresh failed with HTTP %i (transient), will retry: %s",
                       responseCode, spDownload->Data().c_str());
        return AuthRefreshResult::TransientFailure;
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
EDreamClient::HelloResult EDreamClient::HelloDetailed() {
    HelloResult result{false, HelloFailureReason::TransientFailure, 0, "Handshake failed", ""};
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    Network::spCFileDownloader spDownload;

    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        // Check for abort before each attempt to allow fast shutdown
        if (g_NetworkManager->IsAborted()) {
            g_Log->Info("Hello() aborted due to shutdown");
            result.reason = HelloFailureReason::TransientFailure;
            result.message = "Client is shutting down";
            return result;
        }
        
        spDownload = std::make_shared<Network::CFileDownloader>("Hello!");
        Network::NetworkHeaders::addStandardHeaders(spDownload);
        spDownload->AppendHeader("Content-Type: application/json");
        
        // Retrieve the sealed session from settings
        std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
        
        if (sealedSession.empty()) {
            g_Log->Error("Sealed session not found in settings");
            result.reason = HelloFailureReason::InvalidSession;
            result.message = "No saved session token";
            return result;
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
            int responseCode = static_cast<int>(spDownload->ResponseCode());
            result.httpCode = responseCode;
            if (responseCode >= 400 && responseCode < 500)
            {
                if (currentAttempt == maxAttempts) {
                    result.reason = HelloFailureReason::InvalidSession;
                    result.message = "Session token rejected by backend";
                    return result;
                }
                AuthRefreshResult refreshResult = RefreshSealedSession();
                if (refreshResult == AuthRefreshResult::Success) {
                    continue;
                }
                if (refreshResult == AuthRefreshResult::InvalidSession) {
                    result.reason = HelloFailureReason::InvalidSession;
                    result.message = "Session refresh rejected by backend";
                    return result;
                }
                result.reason = HelloFailureReason::TransientFailure;
                result.message = "Session refresh failed transiently";
                return result;
            }
            else
            {
                g_Log->Error("Failed to handshake. Server returned %i: %s",
                             spDownload->ResponseCode(),
                             spDownload->Data().c_str());
                if (currentAttempt == maxAttempts) {
                    result.reason = HelloFailureReason::TransientFailure;
                    result.message = "Backend unavailable for hello handshake";
                    return result;
                }
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

        // Parse quotaExpiresAt if present
        if (data.as_object().if_contains("quotaExpiresAt")) {
            json::value quotaExpiresAtValue = data.at("quotaExpiresAt");
            std::string expiresAtStr = quotaExpiresAtValue.as_string().c_str();
            
            // Parse ISO 8601 datetime string (e.g., "2026-02-27T16:37:09.942Z")
            std::tm tm = {};
            std::istringstream ss(expiresAtStr);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            
            if (!ss.fail()) {
                auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));
                quotaExpiresAt = tp;
                cm.setQuotaExpiresAt(tp);

                g_Log->Info("Hello: Quota expires at %s", expiresAtStr.c_str());
            } else {
                g_Log->Warning("Hello: Failed to parse quotaExpiresAt: %s", expiresAtStr.c_str());
            }
        }

        // Schedule the next quota update (this ensures quota timer is started even if DidSignIn was called earlier)
        ScheduleNextQuotaUpdate();

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

            result.success = true;
            result.reason = HelloFailureReason::None;
            result.playlistUUID = std::string(uuid);
            return result;
        } else {
            g_Log->Info("Handshake with server successful, no playlist, remaining quota : %lld", remainingQuota);
            result.success = true;
            result.reason = HelloFailureReason::None;
            result.playlistUUID = "";
            return result;
        }
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, spDownload->Data());
    }

    result.reason = HelloFailureReason::TransientFailure;
    result.message = "Malformed hello response";
    return result;
}

std::string EDreamClient::Hello() {
    return HelloDetailed().playlistUUID;
}



std::string EDreamClient::GetCurrentServerPlaylist() {
    std::string localPlaylistId = g_Settings()->Get("settings.content.current_playlist_uuid", std::string(""));
    auto invalidateSessionAndPromptRelogin = []() {
        g_Log->Warning("Invalidating local session and prompting user to sign in again");
        // Prefer the full sign-out path to keep client state consistent and
        // to ensure the existing HUD messaging ("Please open settings to sign in.")
        // is shown via IsLoggedIn() == false.
        try {
            EDreamClient::SignOut();
        } catch (...) {
            // Defensive: if something throws, still clear local auth state.
            fIsLoggedIn.exchange(false);
            g_Settings()->Set("settings.content.sealed_session", std::string(""));
            g_Settings()->Set("settings.content.refresh_token", std::string(""));
            g_Settings()->Storage()->Commit();
            g_Player().SetOfflineMode(true);
        }
        ESShowPreferences();
    };
    // Handshake server and get quota and current playlist ID
    auto hello = HelloDetailed();

    if (!hello.success) {
        if (hello.reason == HelloFailureReason::InvalidSession) {
            g_Log->Warning("Hello returned invalid session (HTTP %d): %s. Signing out and asking user to relogin.",
                           hello.httpCode, hello.message.c_str());
            invalidateSessionAndPromptRelogin();
            return localPlaylistId;
        }

        g_Log->Warning("Hello transient failure (HTTP %d): %s. Staying signed in and using local playlist.",
                       hello.httpCode, hello.message.c_str());
        static std::atomic<bool> sHelloRetryLoopRunning(false);
        bool expected = false;
        if (sHelloRetryLoopRunning.compare_exchange_strong(expected, true)) {
            boost::thread([invalidateSessionAndPromptRelogin]() {
                constexpr int kRetryDelayInitialSeconds = 60;
                constexpr int kRetryDelayMultiplier = 3;
                constexpr int kRetryDelayMaxSeconds = 24 * 3600;
                int delaySeconds = kRetryDelayInitialSeconds;

                while (EDreamClient::IsLoggedIn()) {
                    g_Log->Info("Hello retry scheduled in %d seconds", delaySeconds);
                    boost::this_thread::sleep(boost::posix_time::seconds(delaySeconds));
                    auto retry = EDreamClient::HelloDetailed();
                    if (retry.success) {
                        g_Log->Info("Hello retry succeeded");
                        break;
                    }
                    if (retry.reason == HelloFailureReason::InvalidSession) {
                        g_Log->Warning("Hello retry reported invalid session, signing out");
                        invalidateSessionAndPromptRelogin();
                        break;
                    }
                    delaySeconds = std::min(delaySeconds * kRetryDelayMultiplier, kRetryDelayMaxSeconds);
                }
                sHelloRetryLoopRunning.store(false);
            }).detach();
        }
        return localPlaylistId;
    }
    auto playlistId = hello.playlistUUID;
    
    // Fetch playlist if we have one and save it to disk
    if (playlistId != "") {
        FetchPlaylist(playlistId);
    } else {
        FetchDefaultPlaylist();
    }
    
    return playlistId;
}

// MARK: - Telemetry functions
void EDreamClient::SendTelemetry(const std::string& eventType, const boost::json::object& eventData) {
    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("Telemetry");
    Network::NetworkHeaders::addStandardHeaders(spDownload);
    spDownload->AppendHeader("Content-Type: application/json");
    
    std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
    if (!sealedSession.empty()) {
        std::string cookieHeader = "Cookie: wos-session=" + sealedSession;
        spDownload->AppendHeader(cookieHeader);
    }

    boost::json::object payload;
    payload["eventType"] = eventType;
    payload["eventData"] = eventData;
    payload["clientVersion"] = PlatformUtils::GetAppVersion();
    payload["clientPlatform"] = PlatformUtils::GetPlatformName();
    
    std::string jsonBody = boost::json::serialize(payload);
    spDownload->SetPostFields(jsonBody.c_str());

    std::string url = ServerConfig::ServerConfigManager::getInstance().getEndpoint(ServerConfig::Endpoint::TELEMETRY);
    
    if (!spDownload->Perform(url)) {
        g_Log->Error("Failed to send telemetry. Server returned %i", spDownload->ResponseCode());
    }
}

void EDreamClient::ReportMD5Failure(const std::string& uuid, const std::string& foundMd5, bool isStreaming) {
    boost::json::object eventData;
    eventData["uuid"] = uuid;
    eventData["foundMd5"] = foundMd5;
    eventData["isStreaming"] = isStreaming;
    
    SendTelemetry("md5-mismatch", eventData);
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
        
        g_Log->Info("Playlist fetched");
        // Wait for the fetch to complete
        bool fetchSuccess = fetchFuture.get();
        
        if (!fetchSuccess) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", uuid.c_str());
            return false;
        }

        // save the current playlist id, this will get reused at next startup
        g_Settings()->Set("settings.content.current_playlist_uuid", uuid);
        
        std::thread([uuid]() {
            // These operations must happen on the main/UI thread
            g_Log->Info("Will call set playlist");
            g_Player().SetPlaylist(std::string(uuid), false);
            g_Player().SetTransitionDuration(1.0f);
            g_Log->Info("Will call start transition");
            g_Player().StartTransition();
        }).detach();
        
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
                if (RefreshSealedSession() != AuthRefreshResult::Success)
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
    // Check for abort at start to allow fast shutdown
    if (g_NetworkManager->IsAborted()) {
        g_Log->Info("FetchPlaylist() aborted due to shutdown");
        return false;
    }
    
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
        // Check for abort before each attempt to allow fast shutdown
        if (g_NetworkManager->IsAborted()) {
            g_Log->Info("FetchPlaylist() aborted due to shutdown");
            return false;
        }
        
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
                if (RefreshSealedSession() != AuthRefreshResult::Success)
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
        // Check for abort before each attempt to allow fast shutdown
        if (g_NetworkManager->IsAborted()) {
            g_Log->Info("FetchDefaultPlaylist() aborted due to shutdown");
            return false;
        }
        
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
                if (RefreshSealedSession() != AuthRefreshResult::Success)
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
                if (RefreshSealedSession() != AuthRefreshResult::Success)
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
                if (RefreshSealedSession() != AuthRefreshResult::Success)
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
                if (RefreshSealedSession() != AuthRefreshResult::Success)
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


std::vector<PlaylistEntry> EDreamClient::ParsePlaylist(std::string_view uuid) {
    g_Log->Info("Parse Playlist %s", (uuid == "" ? "default playlist" : uuid));
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    // Note: Download queue is no longer used - downloads are pulled dynamically from PlaylistManager

    // Collect all UUIDs/keyframes from the json for individual dreams
    // We also check via our cache if we have metadata or need to download
    std::vector<PlaylistEntry> entries;
    std::vector<std::string> needsMetadataUuids;

    // Open playlist and grab content. Default playlist is named playlist_0
    fs::path filePath = Cache::PathManager::getInstance().jsonPlaylistPath() / (uuid == "" ? "playlist_0.json" : "playlist_" + std::string(uuid) + ".json");

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.string().c_str());
        return entries;
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
            
            auto dreamUuid = std::string(itemObj["uuid"].as_string());
            auto timestamp = itemObj["timestamp"].as_int64();
            
            // Create PlaylistEntry with optional keyframe fields
            std::optional<std::string> startKeyframe;
            std::optional<std::string> endKeyframe;

            // Check for optional keyframe fields
            if (itemObj.contains("start_keyframe")) {
                auto& startVal = itemObj["start_keyframe"];
                if (!startVal.is_null()) {
                    startKeyframe = std::string(startVal.as_string());
                }
            }
            if (itemObj.contains("end_keyframe")) {
                auto& endVal = itemObj["end_keyframe"];
                if (!endVal.is_null()) {
                    endKeyframe = std::string(endVal.as_string());
                }
            }
            
            // Create the entry
            PlaylistEntry entry(dreamUuid, startKeyframe, endKeyframe);
            
            
            // Do we have the metadata?
            if (cm.needsMetadata(dreamUuid, timestamp)) {
                needsMetadataUuids.push_back(dreamUuid.c_str());
            }
            
            // Do we have the video?
            if (!cm.hasDiskCachedItem(dreamUuid.c_str()))
            {
                if (isFirst) {
                    // Prefetch download link for 1st video from playlist if we don't have it
                    needsStreamingUuid = dreamUuid;
                }
            }
            
            entries.push_back(std::move(entry));
            isFirst = false;
        }
    }
    catch (const boost::system::system_error& e)
    {
        JSONUtil::LogException(e, contents);
    }

    // Do we need to fetch metadata?
    if (!needsMetadataUuids.empty()) {
        if (EDreamClient::IsLoggedIn() && !g_Player().IsOfflineMode()) {
            // send that array and try to fetch all at once
            FetchDreamsMetadata(needsMetadataUuids);

            // Then reload metadata for each of the uuids
            for (const auto& needsMetadata : needsMetadataUuids) {
                cm.reloadMetadata(needsMetadata);
            }
        } else {
            g_Log->Info("Skipping metadata fetch at startup (offline/not logged in). Missing metadata count: %zu",
                        needsMetadataUuids.size());
        }
    }

    // Downloads will be pulled dynamically by FindDreamsThread from PlaylistManager
    
    // Finally, if needed fetch streaming link for 1st video
    if (!needsStreamingUuid.empty() && EDreamClient::IsLoggedIn() && !g_Player().IsOfflineMode()) {
        // Grab a pointer to the dream metadata
        auto dream = cm.getDream(needsStreamingUuid);

        // Grab streaming URL and save it for later use
        g_Log->Info("Parse playlist blocking call for download link");
        auto path = EDreamClient::GetDreamDownloadLink(dream->uuid);
        dream->setStreamingUrl(path);
    } else if (!needsStreamingUuid.empty()) {
        g_Log->Info("Skipping blocking prefetch of streaming link (offline/not logged in). First uncached UUID: %s",
                    needsStreamingUuid.c_str());
    }
    
    return entries;
}

std::tuple<std::string, std::string, bool, int64_t, int> EDreamClient::ParsePlaylistMetadata(std::string_view uuid) {
    // Default playlist doesn't have any metadata right now, hardcoding this
    if (uuid.empty()) {
        return {"Popular dreams", "Various artists", false, 0, 0};
    }

    // Open playlist and grab content
    fs::path filePath = Cache::PathManager::getInstance().jsonPlaylistPath() / ("playlist_" + std::string(uuid) + ".json");
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.string().c_str());
        return {"", "", false, 0, 0};
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
        int loops = playlist.get<int>("loops", 0);

        return {name, artist, nsfw, timestamp, loops};
    }
    catch (const boost::property_tree::json_parser_error& e)
    {
        g_Log->Error("JSON parsing error: %s", e.what());
    }
    catch (const std::exception& e)
    {
        g_Log->Error("Error parsing playlist metadata: %s", e.what());
    }

    return {"", "", false, 0, 0};
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
    g_Player().SetPlaylist(std::string(uuid), false);
    
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
        
        g_Log->Info("should play : %s", uuid.data());
        g_Player().PlayDreamNow(uuid.data(), frameNumber);
    } else if (event == "play_playlist") {
        std::shared_ptr<sio::string_message> uuidObj =
            std::dynamic_pointer_cast<sio::string_message>(response["uuid"]);
        std::string_view uuid = uuidObj->get_string();
        g_Log->Info("should play : %s", uuid.data());
        
        EDreamClient::EnqueuePlaylistAsync(uuid.data());
    }
    else if (event == "like_current_dream")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_LIKE);
    }
    else if (event == "dislike_current_dream")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_DISLIKE);
    }
    else if (event == "report_current_dream")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_REPORT);
    }
    else if (event == "next")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_NEXT);
    }
    else if (event == "previous")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PREVIOUS);
    }
    else if (event == "shuffle")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SHUFFLE);
    }
    else if (event == "forward")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SKIP_FW);
    }
    else if (event == "backward")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SKIP_BW);
    }
    else if (event == "playback_slower")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PLAYBACK_SLOWER);
    }
    else if (event == "playback_faster")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PLAYBACK_FASTER);
    }
    else if (event == "repeat")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_REPEAT);
    }
    else if (event == "help")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_F1);
    }
    else if (event == "status")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_F2);
    }
    else if (event == "pause")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_PAUSE);
    }
    else if (event == "credit")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_CREDIT);
    }
    else if (event == "reset_playlist")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_RESET_PLAYLIST);
    }
    else if (event == "web")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_WEBPAGE);
    }
    else if (event == "brighter")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_BRIGHTNESS_UP);
    }
    else if (event == "darker")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_BRIGHTNESS_DOWN);
    }
    else if (event == "set_speed_1")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_1);
    }
    else if (event == "set_speed_2")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_2);
    }
    else if (event == "set_speed_3")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_3);
    }
    else if (event == "set_speed_4")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_4);
    }
    else if (event == "set_speed_5")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_5);
    }
    else if (event == "set_speed_6")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_6);
    }
    else if (event == "set_speed_7")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_7);
    }
    else if (event == "set_speed_8")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_8);
    }
    else if (event == "set_speed_9")
    {
        g_Client()->EnqueueCommand(
            CElectricSheep::eClientCommand::CLIENT_COMMAND_SPEED_9);
    }
    else
    {
        g_Log->Error("Unknown event type received: %s", event.data());
    }
}

void EDreamClient::SetQuota(long long quota, std::chrono::system_clock::time_point expiresAt)
{
    remainingQuota = quota;
    quotaExpiresAt = expiresAt;

    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    cm.setRemainingQuota(quota);
    cm.setQuotaExpiresAt(expiresAt);

    g_Log->Info("SetQuota: quota updated to %lld", quota);
}

static void OnQuotaUpdate(sio::event& _wsEvent)
{
    std::shared_ptr<sio::object_message> objectMessage =
        std::dynamic_pointer_cast<sio::object_message>(_wsEvent.get_message());
    if (!objectMessage) {
        g_Log->Warning("OnQuotaUpdate: received non-object message");
        return;
    }

    std::map<std::string, sio::message::ptr> response = objectMessage->get_map();

    long long newQuota = 0;
    auto expiresAt = std::chrono::system_clock::time_point{};

    if (response.find("quota") != response.end()) {
        std::shared_ptr<sio::int_message> quotaObj =
            std::dynamic_pointer_cast<sio::int_message>(response["quota"]);
        if (quotaObj) {
            newQuota = quotaObj->get_int();
        }
    }

    if (response.find("quotaExpiresAt") != response.end()) {
        std::shared_ptr<sio::string_message> expiresObj =
            std::dynamic_pointer_cast<sio::string_message>(response["quotaExpiresAt"]);
        if (expiresObj) {
            std::string expiresAtStr = expiresObj->get_string();
            std::tm tm = {};
            std::istringstream ss(expiresAtStr);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (!ss.fail()) {
                expiresAt = std::chrono::system_clock::from_time_t(timegm(&tm));
            }
        }
    }

    if (newQuota > 0) {
        EDreamClient::SetQuota(newQuota, expiresAt);
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
    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        socket->emit("new_remote_control_event", list);
    }
}

void EDreamClient::ConnectRemoteControlSocket()
{
    PlatformUtils::SetThreadName("ConnectRemoteControl");
    
    // Use mutex to prevent concurrent connection attempts
    std::lock_guard<std::mutex> lock(fWebSocketMutex);
    
    g_Log->Info("Performing remote control connect.");
    
    // Check if socket is already connected AND io_context is running AND namespace is available
    auto existingSocket = s_SIOClient.socket("/remote-control");
    if (s_SIOClient.opened() && !io_context->stopped() && existingSocket)
    {
        g_Log->Info("WebSocket already connected with namespace, skipping reconnection.");
        BindWebSocketCallbacks();
        return;
    }

    // If io_context was stopped, restart it
    if (io_context->stopped()) {
        g_Log->Info("io_context was stopped, restarting...");
        io_context->restart();
    }

    // Unbind old callbacks before reconnecting
    EDreamClient::UnbindWebSocketCallbacks();

    std::map<std::string, std::string> query;
    
    std::string sealedSession = g_Settings()->Get("settings.content.sealed_session", std::string(""));
    
    if (sealedSession.empty())
    {
        g_Log->Error("Cannot connect WebSocket: no sealed session available.");
        return;
    }
    
    query["Cookie"] = string_format("wos-session=%s", sealedSession.c_str());
    query["Edream-Client-Type"] = PlatformUtils::GetPlatformName();
    query["Edream-Client-Version"] = PlatformUtils::GetAppVersion();

    g_Log->Info("Connecting to WebSocket server: %s", 
                ServerConfig::ServerConfigManager::getInstance().getWebsocketServer().c_str());
    
    // Always call connect() - Socket.IO client will handle reconnection internally
    s_SIOClient.connect(ServerConfig::ServerConfigManager::getInstance().getWebsocketServer(), query, query);
    
    // Send first ping immediately so frontend knows we're here
    SendPing();

    // Run the io_context in a separate thread
    std::thread([]() {
        io_context->run();
    }).detach();
}

void EDreamClient::Like(std::string uuid) {
    g_Log->Info("Sending like for UUID %s", uuid.c_str());
    
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "like");
    ms->insert("uuid", uuid);
    sio::message::list list;
    list.push(ms);
    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        socket->emit("new_remote_control_event", list);
    }
}

void EDreamClient::Dislike(std::string uuid) {
    g_Log->Info("Sending dislike for UUID %s", uuid.c_str());
    
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "dislike");
    ms->insert("uuid", uuid);
    sio::message::list list;
    list.push(ms);
    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        socket->emit("new_remote_control_event", list);
    }
}

void EDreamClient::Report(std::string uuid) {
    g_Log->Info("Sending report for UUID %s", uuid.c_str());
    
    std::shared_ptr<sio::object_message> ms =
        std::dynamic_pointer_cast<sio::object_message>(
            sio::object_message::create());
    ms->insert("event", "report");
    ms->insert("uuid", uuid);
    sio::message::list list;
    list.push(ms);
    auto socket = s_SIOClient.socket("/remote-control");
    if (socket) {
        socket->emit("new_remote_control_event", list);
    }
}



void EDreamClient::SetCPUUsage(int _cpuUsage) { fCpuUsage.exchange(_cpuUsage); }

