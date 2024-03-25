#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include <memory>
#include <atomic>
#include <mutex>

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

  public:
    static void InitializeClient();
    static void DeinitializeClient();
    static bool FetchPlaylist(int id);
    static std::vector<std::string> ParsePlaylist(int id);
    
    static bool EnqueuePlaylist(int id);
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
