#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include <future>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <tuple>
#include <sstream>
#include <chrono>
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/json.hpp>
#include "Networking.h"
#include "PlaylistManager.h"

class EDreamClient
{
    // Used by SendCode (for now), so we can propagate error messages from the server
    struct AuthResult {
        bool success;
        std::string message;
        
        // Constructor for convenient initialization
        AuthResult(bool s, std::string m = "") : success(s), message(m) {}
    };
    
  private:
    static std::atomic<bool> fIsLoggedIn;
    static std::atomic<bool> fAuthRetryAbort;
    static std::atomic<bool> fAuthRetryPending;
    static std::atomic<bool> fInitialAuthComplete;  // true once first auth refresh attempt has finished
    static std::atomic<int> fCpuUsage;
    static std::mutex fAuthMutex;
    static std::condition_variable fAuthCV;
    static std::mutex fWebSocketMutex;
public:
    static std::atomic<bool> fIsWebSocketConnected;
private:
    static std::string Hello();
    static long long remainingQuota;
    static std::chrono::system_clock::time_point quotaExpiresAt;
    
    static std::future<bool> FetchPlaylistAsync(const std::string& uuid);
    static std::future<bool> FetchDefaultPlaylistAsync();
    static std::future<bool> FetchDreamMetadataAsync(const std::string& uuid);
    static std::future<std::string> GetDreamDownloadLinkAsync(const std::string& uuid);
    static std::future<void> SendPlayingDreamAsync(const std::string& uuid);
    
    static void ParseAndSaveCookies(const Network::spCFileDownloader& spDownload); 

  public:
    static void InitializeClient();
    static void DeinitializeClient();
    static std::string GetCurrentServerPlaylist();

    static std::vector<std::string> FetchUserDislikes();
    static bool FetchPlaylist(std::string_view uuid);
    static bool FetchDefaultPlaylist();
    static bool FetchDreamMetadata(std::string uuid);
    static bool FetchDreamsMetadata(const std::vector<std::string>& uuids);
    static std::string GetDreamDownloadLink(const std::string& uuid);
    
    static std::vector<PlaylistEntry> ParsePlaylist(std::string_view uuid);


    static std::tuple<std::string, std::string, bool, int64_t, int> ParsePlaylistMetadata(std::string_view uuid);

    // Telemetry reporting
    static void SendTelemetry(const std::string& eventType, const boost::json::object& eventData);
    static void ReportMD5Failure(const std::string& uuid, const std::string& foundMd5, bool isStreaming);
    
    static std::future<bool> EnqueuePlaylistAsync(const std::string& uuid);
    static bool EnqueuePlaylist(std::string_view uuid);
    static bool GetDreams(int _page = 0, int _count = -1);

    static bool IsLoggedIn();
    static bool IsAuthRetryPending();  // true while transient auth failure retry loop is running; do not show "log in" UI
    static bool HasCompletedInitialAuth();  // true once first auth refresh attempt has completed (success or failure)
    static bool Authenticate();
    static void SignOut();
    static void DidSignIn();
    // Auth v2
    static AuthResult SendCode();
    /// Same as SendCode(); public return type for UI code outside this class.
    static std::pair<bool, std::string> SendVerificationCodeOutcome();
    static bool ValidateCode(const std::string& code);
    enum class AuthRefreshResult { Success, InvalidSession, TransientFailure };
    static AuthRefreshResult RefreshSealedSession();
    
    static constexpr int DREAMS_PER_PAGE = 10;

  public:
    static void ConnectRemoteControlSocket();
    static void SendPlayingDream(std::string uuid);
    static void SendPing();
    static void SendStateUpdate();  // Send state update immediately when values change
    static void UnbindWebSocketCallbacks();
    static bool IsWebSocketConnected();  // Check actual socket connection status

    static void Like(std::string uuid);
    static void Dislike(std::string uuid);
    static void Report(std::string uuid);
    static void SetCPUUsage(int _cpuUsage);
    static void SetQuota(long long quota, std::chrono::system_clock::time_point expiresAt);

  private:
    static std::unique_ptr<boost::asio::io_context> io_context;
    static std::unique_ptr<boost::asio::steady_timer> ping_timer;
    static std::unique_ptr<boost::asio::steady_timer> quota_timer;
    static void ScheduleNextPing();
    static void ScheduleNextQuotaUpdate();
    static void UpdateQuota();
    static void SendGoodbye();

};

#endif /* _EDREAMCLIENT_H_ */
