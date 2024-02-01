///////////////////////////////////////////////////////////////////////////////
//
//    electricsheep for windows - collaborative screensaver
//    Copyright 2003 Nicholas Long <nlong@cox.net>
//	  electricsheep for windows is based of software
//	  written by Scott Draves <source@electricsheep.org>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef _SHEPHERD_H_
#define _SHEPHERD_H_

#include <queue>
#include <string_view>
#include <string_view>

#include "BlockingQueue.h"
#include "Dream.h"
#include "Log.h"
#include "SmartPtr.h"
#include "Timer.h"
#include "base.h"
#include "boost/atomic.hpp"
#include "boost/detail/atomic_count.hpp"
#include "boost/thread/mutex.hpp"

using namespace std::string_view_literals;

#ifdef WIN32
#define PATH_SEPARATOR_C '\\'
#else
#define PATH_SEPARATOR_C '/'
#endif

constexpr const std::string_view DEFAULT_DREAM_SERVER =
    "https://e-dream-prod-84baba5507ee.herokuapp.com"sv;

#define API_VERSION "/api/v1"

#define DEFINE_ENDPOINT(name, path)                                            \
    {                                                                          \
        ENDPOINT_##name,                                                       \
        {                                                                      \
            std::string(Shepherd::GetDreamServer()) + API_VERSION + path       \
        }                                                                      \
    }

#define SERVER_ENDPOINT_DEFINITIONS                                            \
    DEFINE_ENDPOINT(DREAM, "/dream"), DEFINE_ENDPOINT(LOGIN, "/auth/login"),   \
        DEFINE_ENDPOINT(REFRESH, "/auth/refresh"),                             \
        DEFINE_ENDPOINT(USER, "/auth/user")

enum eServerEndpoint
{
    ENDPOINT_DREAM,
    ENDPOINT_LOGIN,
    ENDPOINT_REFRESH,
    ENDPOINT_USER
};

namespace ContentDownloader
{

enum eServerTargetType
{
    eHostServer,
    eVoteServer,
    eRenderServer
};

class CMessageBody
{
  public:
    CMessageBody(std::string_view _str, const double _duration)
        : m_Msg(_str), m_Duration(_duration){};
    ~CMessageBody(){};

    std::string m_Msg;
    double m_Duration;
};

MakeSmartPointers(CMessageBody);

class CTimedMessageBody
{
  public:
    CTimedMessageBody(const std::string& _str, const double _duration)
        : m_Msg(_str), m_Duration(_duration)
    {
        m_Timer.Reset();
    };
    ~CTimedMessageBody(){};

    bool TimedOut()
    {
        if (m_Timer.Time() > m_Duration)
            return true;

        return false;
    }
    Base::CTimer m_Timer;
    std::string m_Msg;
    double m_Duration;
};

MakeSmartPointers(CTimedMessageBody);

typedef boost::atomic<char*> atomic_char_ptr;

/*!
        Shepherd.
        This class is responsible for managing the threads to create, render,
   and download sheep.
*/
class Shepherd
{
    typedef struct _SHEPHERD_MESSAGE
    {
        char* text;
        int length;
        time_t expire;
    } SHEPHERD_MESSAGE;

    ///	Gets all sheep in path.
    static bool getSheep(const char* path, SheepArray* sheep,
                         const SheepArray& serverFlock);

    static uint64_t s_ClientFlockBytes;
    static uint64_t s_ClientFlockCount;

    static uint64_t s_ClientFlockGoldBytes;
    static uint64_t s_ClientFlockGoldCount;

    static atomic_char_ptr fRootPath;
    static atomic_char_ptr fMp4Path;
    static atomic_char_ptr fJsonPath;
    static atomic_char_ptr fRedirectServerName;
    static atomic_char_ptr fServerName;
    static atomic_char_ptr fVoteServerName;
    static atomic_char_ptr fRenderServerName;
    static atomic_char_ptr fProxy;
    static atomic_char_ptr fProxyUser;
    static atomic_char_ptr fProxyPass;
    static atomic_char_ptr fNickName;
    static int fSaveFrames;
    static int fUseProxy;
    static int fCacheSize;
    static int fCacheSizeGold;
    static int fFuseLen;
    static int fRegistered;
    static atomic_char_ptr fPassword;
    static atomic_char_ptr fUniqueID;
    static bool fShutdown;
    static int fChangeRes;
    static int fChangingRes;
    static boost::detail::atomic_count* renderingFrames;
    static boost::detail::atomic_count* totalRenderedFrames;
    static bool m_RenderingAllowed;

    static std::queue<spCMessageBody> m_MessageQueue;
    static boost::mutex m_MessageQueueMutex;

    static std::vector<spCTimedMessageBody> m_OverflowMessageQueue;
    static boost::mutex s_OverflowMessageQueueMutex;

    static boost::mutex s_ShepherdMutex;

    static boost::shared_mutex s_DownloadStateMutex;

    static boost::shared_mutex s_RenderStateMutex;

    static boost::mutex s_ComputeServerNameMutex;

    static boost::shared_mutex s_GetServerNameMutex;

    static time_t s_LastRequestTime;

    static Base::CBlockingQueue<char*> fStringsToDelete;

    static std::string s_DownloadState;

    static std::string s_RenderState;

    static bool s_IsDownloadStateNew;

    static bool s_IsRenderStateNew;

  public:
    Shepherd();
    ~Shepherd();

    static void initializeShepherd();

    //
    static void setUseProxy(const int& useProxy) { fUseProxy = useProxy; }
    static int useProxy() { return fUseProxy; }

    static void setProxy(const char* proxy);
    static const char* proxy();
    static void setProxyUserName(const char* userName);
    static const char* proxyUserName();
    static void setProxyPassword(const char* password);
    static const char* proxyPassword();

    //
    static void setSaveFrames(const int& saveFrames)
    {
        fSaveFrames = saveFrames;
    }
    static int saveFrames() { return fSaveFrames; }

    //
    static void setRegistered(const int& registered)
    {
        fRegistered = registered;
    }
    static int registered() { return fRegistered; }

    //
    static int cacheSize(const int getGenerationType)
    {
        switch (getGenerationType)
        {
        case 0:
            return fCacheSize;
            break;
        case 1:
            return fCacheSizeGold;
            break;
        default:
            g_Log->Error("Getting cache size for unknown generation type %d",
                         getGenerationType);
            return 0;
        };
    }
    static void setCacheSize(const int& size, const int getGenerationType)
    {

        /*if( (size < 300) && (size > 0) )	fCacheSize = 300;
        else*/
        switch (getGenerationType)
        {
        case 0:
            fCacheSize = size;
            break;
        case 1:
            fCacheSizeGold = size;
            break;
        default:
            g_Log->Error("Setting cache size for unknown generation type %d",
                         getGenerationType);
        };
    }

    //
    static void setChangeRes(const int& changeRes) { fChangeRes = changeRes; }
    static int changeRes() { return fChangeRes; }
    static void setChangingRes(int state) { fChangingRes = state; }
    static void incChangingRes() { fChangingRes++; }
    static int getChangingRes() { return fChangingRes; }

    //
    static void notifyShepherdOfHisUntimleyDeath();

    static void setNewAndDeleteOldString(
        atomic_char_ptr& str, char* newval,
        boost::memory_order mem_ord = boost::memory_order_relaxed);

    /*!
     * @discussion
     *        This method is used to set the root path
     * where all of the files will be created and downloaded
     * to. The method also initializes all of the relative paths
     * for the subdirectories.
     */
    static void setRootPath(const char* path);
    static const char* rootPath();
    static const char* mp4Path();
    static const char* jsonPath();
    static const char* videoExtension() { return ".mp4"; }

    ///	Gets/sets the registration password.
    static void setPassword(const char* password);
    static const char* password();

    ///	Overlay text management for the renderer.
    static void addMessageText(std::string_view s, time_t timeout);

    ///	Called from generators.
    static void FrameStarted();
    static void FrameCompleted();
    static int FramesRendering();
    static int TotalFramesRendered();
    static bool RenderingAllowed();
    static void SetRenderingAllowed(bool _yesno);
    static void SetNickName(const char* nick);
    static const char* GetNickName();
    static const char* GetDreamServer();
    static const char* GetEndpoint(eServerEndpoint _endpoint);

    static bool AddOverflowMessage(const std::string _msg)
    {
        boost::mutex::scoped_lock lockthis(s_OverflowMessageQueueMutex);
        m_OverflowMessageQueue.emplace_back(new CTimedMessageBody(_msg, 60.));
        return true;
    }

    static bool PopOverflowMessage(std::string& _dst)
    {
        boost::mutex::scoped_lock lockthis(s_OverflowMessageQueueMutex);
        if (m_OverflowMessageQueue.size() > 10)
        {
            m_OverflowMessageQueue.erase(
                m_OverflowMessageQueue.begin(),
                m_OverflowMessageQueue.begin() +
                    static_cast<
                        std::vector<spCTimedMessageBody>::difference_type>(
                        m_OverflowMessageQueue.size()) -
                    10);
        }
        if (m_OverflowMessageQueue.size() > 0)
        {
            bool del = false;
            size_t deletestart = 0;
            size_t deleteend = 0;
            for (size_t ii = 0; ii != m_OverflowMessageQueue.size(); ++ii)
            {
                spCTimedMessageBody msg = m_OverflowMessageQueue[ii];
                _dst += msg->m_Msg;
                if (msg->TimedOut())
                {
                    if (del == false)
                    {
                        deletestart = ii;
                        deleteend = ii;
                    }
                    else
                    {
                        deleteend = ii;
                    }
                    del = true;
                }
                _dst += "\n";
            }
            _dst.erase(_dst.find_last_of("\n"));

            if (del == true)
            {
                m_OverflowMessageQueue.erase(
                    m_OverflowMessageQueue.begin() +
                        static_cast<
                            std::vector<spCTimedMessageBody>::difference_type>(
                            deletestart),
                    m_OverflowMessageQueue.begin() +
                        static_cast<
                            std::vector<spCTimedMessageBody>::difference_type>(
                            deleteend) +
                        1);
            }

            return true;
        }
        return false;
    }

    static bool QueueMessage(std::string_view _msg, const double _duration)
    {
        boost::mutex::scoped_lock lockthis(m_MessageQueueMutex);
        m_MessageQueue.emplace(new CMessageBody(_msg, _duration));
        return true;
    }

    static bool PopMessage(std::string& _dst, double& _duration)
    {
        boost::mutex::scoped_lock lockthis(m_MessageQueueMutex);
        if (m_MessageQueue.size() > 0)
        {
            spCMessageBody msg = m_MessageQueue.front();
            _dst = msg->m_Msg;
            _duration = msg->m_Duration;
            m_MessageQueue.pop();
            return true;
        }
        return false;
    }

    //	Method to get all of the sheep the exist on the client.
    static bool getClientFlock(SheepArray* sheep);
    static uint64_t GetFlockSizeMBsRecount(const int generationtype);

    //	Sets/Gets the unique id for this Shepherd.
    static void setUniqueID(const char* uniqueID);
    static const char* uniqueID();

    static void setDownloadState(const std::string& state);
    static std::string downloadState(bool& isnew);

    static void setRenderState(const std::string& state);
    static std::string renderState(bool& isnew);

    static void subClientFlockBytes(uint64_t removedbytes,
                                    const int generationtype)
    {
        if (generationtype == 0)
            s_ClientFlockBytes -= removedbytes;
        if (generationtype == 1)
            s_ClientFlockGoldBytes -= removedbytes;
    }
    static void subClientFlockCount(const int generationtype)
    {
        if (generationtype == 0)
            --s_ClientFlockCount;
        if (generationtype == 1)
            --s_ClientFlockGoldCount;
    }

    static uint64_t getClientFlockMBs(const int generationtype)
    {
        if (generationtype == 0)
            return s_ClientFlockBytes / 1024 / 1024;
        if (generationtype == 1)
            return s_ClientFlockGoldBytes / 1024 / 1024;
        return 0;
    }
    static uint64_t getClientFlockCount(const int generationtype)
    {
        if (generationtype == 0)
            return s_ClientFlockCount;
        if (generationtype == 1)
            return s_ClientFlockGoldCount;
        return 0;
    }
};

}; // namespace ContentDownloader
#endif
