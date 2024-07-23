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
#include "EDreamClient.h"
#include "Networking.h"
#include "Player.h"
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

void DreamDownloader::AddDreamUUID(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dreamUUIDs.insert(uuid);
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

bool DreamDownloader::isDreamUUIDQueued(const std::string& uuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dreamUUIDs.find(uuid) != m_dreamUUIDs.end();
}

// MARK: Thread function
void DreamDownloader::FindDreamsThread() {
    PlatformUtils::SetThreadName("FindDreamsToDownload");
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    // Set a default delay (30 seconds)
    int delayTime = 30;
    
    // Minimum disk space : 5 GB (TODO: this can be lowered or pref'd later)
    std::uintmax_t minDiskSpace =  (std::uintmax_t)1024 * 1024 * 1024 * 5;
    // Minimum space in cache/quota to consider downloading (10 MB)
    std::uintmax_t minSpaceForDream = 1024 * 1024 * 10;

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
            if (!cm.hasDiskCachedItem(current_uuid.c_str())) {
                auto link = EDreamClient::GetDreamDownloadLink(current_uuid);
                
                if (!link.empty()) {
                    g_Log->Error("Download link received: %s", link.c_str());
                    DownloadDream(current_uuid, link);
                    
                    // Wait 30s
                    boost::this_thread::sleep(boost::get_system_time() +
                                         boost::posix_time::seconds(30));
                } else {
                    g_Log->Error("Download link denied");
                }

            }
            
        }

        // Sleep for a while before the next iteration
        boost::this_thread::sleep(boost::get_system_time() +
                             boost::posix_time::seconds(30));
    }
    g_Log->Info("Exiting FindDreamsThreads()");
}


bool DreamDownloader::DownloadDream(const std::string& uuid, const std::string& downloadLink) {
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    std::string saveFolder = Shepherd::mp4Path();

    if (uuid.empty() || downloadLink.empty() || saveFolder.empty()) {
        g_Log->Error("Invalid input parameters for DownloadDream");
        return false;
    }

    auto dream = cm.getDream(uuid);
    
    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("dream " + dream->name);
    
    // Set up headers if needed (not needed if we stay on S3)
    //spDownload->AppendHeader("Content-Type: application/octet-stream");
    //std::string authHeader{string_format("Authorization: Bearer %s", GetAccessToken())};
    //spDownload->AppendHeader(authHeader);

    // Perform the download
    if (!spDownload->Perform(downloadLink)) {
        g_Log->Error("Failed to download dream. Server returned %i: %s",
                     spDownload->ResponseCode(),
                     spDownload->Data().c_str());
        return false;
    }

    // Ensure the save folder exists
    std::filesystem::path savePath(saveFolder);
    try {
        if (!std::filesystem::exists(savePath)) {
            std::filesystem::create_directories(savePath);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        g_Log->Error("Failed to create mp4 directory: %s", e.what());
        return false;
    }

    // Determine file extension
    std::string fileExtension = ".mp4";

    // Construct the full save path
    std::string fullSavePath = (savePath / (uuid + fileExtension)).string();

    // Save the downloaded content to file
    if (!spDownload->Save(fullSavePath)) {
        g_Log->Error("Failed to save downloaded dream to %s", fullSavePath.c_str());
        return false;
    }

    g_Log->Info("Successfully downloaded and saved dream to %s", fullSavePath.c_str());
    
    Cache::CacheManager::DiskCachedItem newDiskItem;
    newDiskItem.uuid = uuid;
    newDiskItem.version = dream->video_timestamp;
    newDiskItem.downloadDate = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    cm.addDiskCachedItem(newDiskItem);
    
    // Add the file to the player too
    g_Player().Add(fullSavePath.c_str());

    
    return true;
}

}
