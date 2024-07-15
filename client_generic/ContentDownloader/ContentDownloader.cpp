#ifdef WIN32
#include <windows.h>
#endif
#include <list>
#include <string>
#include <vector>

#include "Log.h"
#include "Networking.h"
#include "Settings.h"
#include "base.h"
#include "clientversion.h"

#include "ContentDownloader.h"
#include "SheepDownloader.h"
#include "Shepherd.h"
#ifdef MAC
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace ContentDownloader
{

/*
 */
CContentDownloader::CContentDownloader() {}

//
static std::string generateID()
{
    uint8_t* salt;
    uint32_t u;
    char id[17];
    id[16] = 0;

#ifdef WIN32
    SYSTEMTIME syst;
    GetSystemTime(&syst);
    salt = ((unsigned char*)&syst) + sizeof(SYSTEMTIME) - 8;
#else
    timeval cur_time;
    gettimeofday(&cur_time, NULL);

    salt = (unsigned char*)&cur_time;
#endif

    for (u = 0; u < 16; u++)
    {
        unsigned r = static_cast<unsigned>(rand());
        r = r ^ (salt[u >> 1] >> ((u & 1) << 2));
        r &= 15;
        if (r < 10)
            r += '0';
        else
            r += 'A' - 10;

        id[u] = static_cast<char>(r & 0xFF);
    }

    return std::string(id);
}

//
void CContentDownloader::ServerFallback() { Shepherd::setRegistered(false); }

/*
 */
bool CContentDownloader::Startup(const bool _bPreview, bool _bReadOnlyInstance)
{
    g_Log->Info("Attempting to start contentdownloader...", _bPreview);
    Shepherd::initializeShepherd();

    std::string root = g_Settings()->Get("settings.content.sheepdir",
                                         g_Settings()->Root() + "content");

    if (root.empty())
    {
        root = g_Settings()->Root() + "content";
        g_Settings()->Set("settings.content.sheepdir", root);
    }

    Shepherd::setRootPath(root.c_str());
    Shepherd::setCacheSize(
        g_Settings()->Get("settings.content.cache_size", 2000), 0);
    if (g_Settings()->Get("settings.content.unlimited_cache", true) == true)
        Shepherd::setCacheSize(0, 0);
    Shepherd::setUniqueID(
        g_Settings()->Get("settings.content.unique_id", generateID()).c_str());
    Shepherd::setUseProxy(
        g_Settings()->Get("settings.content.use_proxy", false));
    Shepherd::setRegistered(
        g_Settings()->Get("settings.content.registered", false));
    Shepherd::setProxy(
        g_Settings()->Get("settings.content.proxy", std::string("")).c_str());
    Shepherd::setProxyUserName(
        g_Settings()
            ->Get("settings.content.proxy_username", std::string(""))
            .c_str());
    Shepherd::setProxyPassword(
        g_Settings()
            ->Get("settings.content.proxy_password", std::string(""))
            .c_str());
    // Shepherd::setUseDreamAI( false );

    Shepherd::setSaveFrames(
        g_Settings()->Get("settings.generator.save_frames", false));
    Shepherd::SetNickName(
        g_Settings()
            ->Get("settings.generator.nickname", std::string(""))
            .c_str());

    
    // Initialize new downloader
    if (_bReadOnlyInstance == false)
    {
        // Thread is self contained here
        m_gDownloader.FindDreamsToDownload();
    } 
    else 
    {
        g_Log->Warning("Downloading disabled.");
    }
    /*
    m_gDownloader = new SheepDownloader();

    if (_bReadOnlyInstance == false)
    {
        g_Log->Info("Starting download thread...");
        m_gDownloadThread = new boost::thread(
            boost::bind(&SheepDownloader::shepherdCallback, m_gDownloader));
#ifdef WIN32
        SetThreadPriority((HANDLE)m_gDownloadThread->native_handle(),
                          THREAD_PRIORITY_BELOW_NORMAL);
        SetThreadPriorityBoost((HANDLE)m_gDownloadThread->native_handle(),
                               TRUE);
#else
        struct sched_param sp;
        sp.sched_priority = 6; // Background NORMAL_PRIORITY_CLASS -
                               // THREAD_PRIORITY_BELOW_NORMAL
        pthread_setschedparam((pthread_t)m_gDownloadThread->native_handle(),
                              SCHED_RR, &sp);
#endif
    }
    else
        g_Log->Warning("Downloading disabled.");

     */
    g_Log->Info("...success");

    return true;
}

/*
 */
bool CContentDownloader::Shutdown(void)
{
    //	Terminate the threads.
    g_Log->Info("Terminating download thread.");

    g_NetworkManager->Abort();

/*    if (m_gDownloadThread && m_gDownloader)
    {
        m_gDownloader->Abort();
        m_gDownloadThread->interrupt();

        m_gDownloadThread->timed_join(boost::posix_time::seconds(3));

        SAFE_DELETE(m_gDownloadThread);
    }*/

    //SAFE_DELETE(m_gDownloader);

    //	Notify the shepherd that the app is about to close so that he can
    // properly clean up his threads
    Shepherd::notifyShepherdOfHisUntimleyDeath();

    return true;
}

/*
 */
CContentDownloader::~CContentDownloader()
{
    //	Mark singleton as properly shutdown, to track unwanted access after this
    // point.
    SingletonActive(false);
}

}; // namespace ContentDownloader
