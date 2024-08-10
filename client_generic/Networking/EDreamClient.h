#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include <memory>
#include <atomic>
#include <mutex>
#include <tuple>

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
    
  public:
    static void InitializeClient();
    static void DeinitializeClient();
    static std::string GetCurrentServerPlaylist();
    static bool FetchPlaylist(std::string_view uuid);
    static bool FetchDefaultPlaylist();
    static bool FetchDreamMetadata(std::string uuid);
    static std::string GetDreamDownloadLink(const std::string& uuid);
    static std::vector<std::string> ParsePlaylist(std::string_view uuid);
    static std::tuple<std::string, std::string> ParsePlaylistCredits(std::string_view uuid);

    static bool EnqueuePlaylist(std::string_view uuid);
    static bool GetDreams(int _page = 0, int _count = -1);
    static const char* GetAccessToken();
    static bool RefreshAccessToken();
    static bool IsLoggedIn();
    static bool Authenticate();
    static void SignOut();
    static void DidSignIn(const std::string& _authToken,
                          const std::string& _refreshToken);
    static constexpr int DREAMS_PER_PAGE = 10;

  public:
    static void ConnectRemoteControlSocket();
    static void SendPlayingDream(std::string uuid);

  public:
    static void SetCPUUsage(int _cpuUsage);
};

#endif /* _EDREAMCLIENT_H_ */
