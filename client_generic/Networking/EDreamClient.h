#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include    <boost/atomic.hpp>
#include    <boost/thread/mutex.hpp>

class EDreamClient
{
private:
    static boost::atomic<char *> fAccessToken;
    static boost::atomic<char *> fRefreshToken;
    static boost::atomic<bool> fIsLoggedIn;
    static boost::mutex fAuthMutex;
public:
    static void InitializeClient();
    static bool GetDreams();
    static const char* GetAccessToken();
    static bool RefreshAccessToken();
    static bool IsLoggedIn();
    static bool Authenticate();
};

#endif /* _EDREAMCLIENT_H_ */
