#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include <memory>
#include <boost/atomic.hpp>
#include <boost/thread/mutex.hpp>

class EDreamClient
{
  private:
    static boost::atomic<char*> fAccessToken;
    static boost::atomic<char*> fRefreshToken;
    static boost::atomic<bool> fIsLoggedIn;
    static boost::atomic<int> fCpuUsage;
    static boost::mutex fAuthMutex;

  public:
    static void InitializeClient();
    static bool GetDreams();
    static const char* GetAccessToken();
    static bool RefreshAccessToken();
    static bool IsLoggedIn();
    static bool Authenticate();
    static void SignOut();
    static void DidSignIn(const std::string& _authToken,
                          const std::string& _refreshToken);

  public:
    static void ConnectRemoteControlSocket();

  public:
    static void SetCPUUsage(int _cpuUsage);
};

#endif /* _EDREAMCLIENT_H_ */
