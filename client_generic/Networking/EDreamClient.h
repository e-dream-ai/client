#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include <future>
#include <memory>
#include <atomic>
#include <mutex>
#include <tuple>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

class EDreamClient
{
  private:
    static std::atomic<char*> fAccessToken;
    static std::atomic<char*> fRefreshToken;
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
    static const char* GetAccessToken();
    static bool RefreshAccessToken();
    static bool IsLoggedIn();
    static bool Authenticate();
    static void SignOut();
    static void DidSignIn(const std::string& _authToken,
                          const std::string& _refreshToken);
    // Auth v2
    static bool SendCode();
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
