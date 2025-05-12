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
#include "EDreamClient.h"

#include "ContentDownloader.h"
//#include "SheepDownloader.h"
//#include "Shepherd.h"
#ifdef MAC
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace ContentDownloader
{

/*
 */
CContentDownloader::CContentDownloader() {
    // Starting client init
    EDreamClient::InitializeClient();
}

//
void CContentDownloader::ServerFallback() {
    g_Log->Error("Server Fallback");
    //Shepherd::setRegistered(false);
}

/*
 */
bool CContentDownloader::Startup(const bool _bPreview, bool _bReadOnlyInstance)
{
    g_Log->Info("Attempting to start contentdownloader...", _bPreview);
    
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
    
    g_Log->Info("...success");

    return true;
}

/*
 */
bool CContentDownloader::Shutdown(void)
{
    //	Terminate the threads.
    g_Log->Info("Terminating download thread.");
    
    // Clear the queue
    m_gDownloader.ClearDreamUUIDs();
    
    // Cut download manager
    g_NetworkManager->Abort();

    // End the dream finding thread
    m_gDownloader.StopFindingDreams();
    
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
