#ifndef _EDREAMCLIENT_H_
#define _EDREAMCLIENT_H_

#include    "boost/atomic.hpp"

class EDreamClient
{
private:
    static boost::atomic<char *> fAccessToken;
    static boost::atomic<char *> fRefreshToken;
public:
    static void InitializeClient();
    static bool GetDreams();
    static const char* GetAccessToken();
    static void RefreshAccessToken();
};

#endif /* _EDREAMCLIENT_H_ */
