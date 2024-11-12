#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include <future>
#include <memory>
#include <atomic>
#include <mutex>
#include <tuple>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include "Networking.h"

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
    static std::atomic<int> fCpuUsage;
    static std::mutex fAuthMutex;
    static std::mutex fWebSocketMutex;
    static bool fIsWebSocketConnected;
    static std::string Hello();
    static long long remainingQuota;
    
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
    static std::string GetDreamDownloadLink(const std::string& uuid);
    static std::vector<std::string> ParsePlaylist(std::string_view uuid);
    static std::tuple<std::string, std::string, bool, int64_t> ParsePlaylistMetadata(std::string_view uuid);
    
    static std::future<bool> EnqueuePlaylistAsync(const std::string& uuid);
    static bool EnqueuePlaylist(std::string_view uuid);
    static bool GetDreams(int _page = 0, int _count = -1);

    static bool IsLoggedIn();
    static bool Authenticate();
    static void SignOut();
    static void DidSignIn();
    // Auth v2
    static AuthResult SendCode();
    static bool ValidateCode(const std::string& code);
    static bool RefreshSealedSession();
    
    static constexpr int DREAMS_PER_PAGE = 10;

  public:
    static void ConnectRemoteControlSocket();
    static void SendPlayingDream(std::string uuid);
    static void SendPing();

    static void Like(std::string uuid);
    static void Dislike(std::string uuid);
    static void SetCPUUsage(int _cpuUsage);

  private:
    static std::unique_ptr<boost::asio::io_context> io_context;
    static std::unique_ptr<boost::asio::steady_timer> ping_timer;
    static void ScheduleNextPing();
    static void SendGoodbye();

};

#endif /* _EDREAMCLIENT_H_ */
