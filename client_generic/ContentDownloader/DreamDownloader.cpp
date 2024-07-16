//
//  DreamDownloader.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 15/07/2024.
//

#include "DreamDownloader.h"
#include "CacheManager.h"
#include "PlatformUtils.h"
#include "Shepherd.h"
#include "Log.h"

namespace ContentDownloader
{

void DreamDownloader::FindDreamsToDownload() {
    
    if (isRunning.exchange(true)) {
        std::cout << "FindDreamsToDownload is already running." << std::endl;
        return;
    }

    thread = boost::thread(&DreamDownloader::FindDreamsThread, this);
    
}

void DreamDownloader::AddDreamUUIDs(const std::vector<std::string>& uuids) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& uuid : uuids) {
        m_dreamUUIDs.insert(uuid);
    }
}

size_t DreamDownloader::GetDreamUUIDCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dreamUUIDs.size();
}

// MARK: Thread function
void DreamDownloader::FindDreamsThread() {
    PlatformUtils::SetThreadName("FindDreamsToDownload");
    int delayTime = 30; // Set a default delay
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    
    while (isRunning.load()) {
        g_Log->Info("Searching for dreams to download...");

        // Make sure we have a properly initialized CacheManager
        if (cm.dreamCount() < 1) {
            g_Log->Info("CacheManager isn't primed yet");
            boost::this_thread::sleep(boost::get_system_time() +
                                 boost::posix_time::seconds(5));
            continue;
        }
        
        
        
        while (true) {
            // Preflight checklist
            
            // Minimum disk space : 10 GB (TODO: this can be lowered or pref'd later)
            std::uintmax_t minDiskSpace =  (std::uintmax_t)1024 * 1024 * 1024 * 10;
            // Minimum space in cache/quota to consider downloading (10 MB)
            std::uintmax_t minSpaceForDream = 1024 * 1024 * 10;
 
            // Make sure we have some remaining quota
            if (cm.getRemainingQuota() < (long long)minSpaceForDream) {
                g_Log->Info("Quota too low to grab new videos %ll", cm.getRemainingQuota());
                break;
            }
            
            // Do we even have some disk space available ?
            if (cm.getFreeSpace(Shepherd::mp4Path()) < minDiskSpace) {
                g_Log->Info("Disk space too low %ll", cm.getFreeSpace(Shepherd::mp4Path()));
                break;
            }

            // Is our cache full ?
            if (cm.getRemainingCacheSpace() < minSpaceForDream) {
                // TODO: add trigger to cache cleanup and decision process
                g_Log->Info("Not enough space in cache remaining %ll", cm.getRemainingCacheSpace());
                break;
            }
            // /Preflight
            
            
            // We look through the list of uuids we've been given
            std::string current_uuid;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_dreamUUIDs.empty()) {
                    break;
                }
                auto it = m_dreamUUIDs.begin();
                current_uuid = *it;
                m_dreamUUIDs.erase(it);
            }
            
            g_Log->Info("Processing dream with UUID: %s", current_uuid.c_str());
            // TODO : add our cache check + download initialisation here
            
        }

        // Sleep for a while before the next iteration
        boost::this_thread::sleep(boost::get_system_time() +
                             boost::posix_time::seconds(30));
    }
    g_Log->Info("Exiting FindDreamsThreads()");
}


}
