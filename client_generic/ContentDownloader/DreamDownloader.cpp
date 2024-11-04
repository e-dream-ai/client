//
//  DreamDownloader.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 15/07/2024.
//

#include "DreamDownloader.h"
#include "CacheManager.h"
#include "PlatformUtils.h"
#include "PathManager.h"
#include "EDreamClient.h"
#include "Networking.h"
#include "Player.h"
#include "Log.h"
#include "NetworkConfig.h"

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
// MARK: Download status
void DreamDownloader::SetDownloadStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_downloadStatus = status;
}

std::string DreamDownloader::GetDownloadStatus() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_downloadStatus;
}

// MARK: Thread function
void DreamDownloader::FindDreamsThread() {
    PlatformUtils::SetThreadName("FindDreamsToDownload");
    
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    // Set a default delay (30 seconds)
    // int delayTime = 30;
    
    // Minimum disk space : 5 GB (TODO: this can be lowered or pref'd later)
    std::uintmax_t minDiskSpace =  (std::uintmax_t)1024 * 1024 * 1024 * 5;
    // Minimum space in cache/quota to consider downloading (100 MB)
    std::uintmax_t minSpaceForDream = (std::uintmax_t)1024 * 1024 * 100;

    while (isRunning.load()) {
        //g_Log->Info("Searching for dreams to download...");
        SetDownloadStatus("Searching for dreams to download...");

        // Make sure we have a properly initialized CacheManager
        if (cm.dreamCount() < 1) {
            g_Log->Info("Initializing...");
            SetDownloadStatus("Initializing...");

            boost::this_thread::sleep(boost::get_system_time() +
                                 boost::posix_time::seconds(5));
            continue;
        }
        
        
        
        while (true) {
            // Preflight checklist
 
            // Make sure we have some remaining quota
            if (cm.getRemainingQuota() < (long long)minSpaceForDream) {
                g_Log->Info("Quota too low to grab new videos %ll", cm.getRemainingQuota());
                // TODO : some user funnel to add quota, later on
                SetDownloadStatus("Your quota is expired");

                break;
            }
            
            // Do we even have some disk space available ?
            if (cm.getFreeSpace(Cache::PathManager::getInstance().mp4Path()) < minDiskSpace) {
                g_Log->Info("Not enough disk space %ll", cm.getFreeSpace(Cache::PathManager::getInstance().mp4Path()));
                SetDownloadStatus("Not enough disk space");
                break;
            }

            // Is our cache full ?
            if (cm.getRemainingCacheSpace() < minSpaceForDream) {
                // we try to remove the oldest video that's not part of the playlist. if that fails, bails
                if (!cm.removeOldestVideo(true)) {
                    g_Log->Info("Not enough space in cache remaining : %ju, no video to remove outside playlist", cm.getRemainingCacheSpace());
                    SetDownloadStatus("Your cache is full");
                    break;
                }
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
                    g_Log->Info("Download link received: %s", link.c_str());
                    auto queueSize = m_dreamUUIDs.size() + 1;
                    if (queueSize == 1) {
                        SetDownloadStatus("Downloading 1 dream ");
                    } else {
                        std::string status = "Downloading " + std::to_string(queueSize) + " dreams";
                        SetDownloadStatus(status);
                    }

                    DownloadDream(current_uuid, link);
                    
                } else {
                    g_Log->Error("Download link denied");
                }

            }
            
        }

        //SetDownloadStatus("No dream to download");
        // Sleep for a while before the next iteration
        boost::this_thread::sleep(boost::get_system_time() +
                             boost::posix_time::seconds(10));
    }
    g_Log->Info("Exiting FindDreamsThreads()");
}

std::future<bool> DreamDownloader::DownloadImmediately(const std::string& uuid, std::function<void(bool, const std::string&)> callback) {
    return std::async(std::launch::async,
        [this, uuid, callback]() {
            return this->DownloadDreamNow(uuid, callback);
        }
    );
}

bool DreamDownloader::DownloadDream(const std::string& uuid, const std::string& downloadLink, bool enqueue) {
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    fs::path savePath = Cache::PathManager::getInstance().mp4Path();

    if (uuid.empty() || downloadLink.empty() || savePath.empty()) {
        g_Log->Error("Invalid input parameters for DownloadDream");
        return false;
    }

    auto dream = cm.getDream(uuid);
    
    Network::spCFileDownloader spDownload = std::make_shared<Network::CFileDownloader>("Downloading dream " + dream->name);
    Network::NetworkHeaders::addStandardHeaders(spDownload);
    
    if (!spDownload->Perform(downloadLink)) {
        SetDownloadStatus("Download failed for " + dream->name);
        g_Log->Error("Failed to download dream. Server returned %i: %s",
                     spDownload->ResponseCode(),
                     spDownload->Data().c_str());
        return false;
    }

    try {
        if (!fs::exists(savePath)) {
            fs::create_directories(savePath);
        }
    } catch (const fs::filesystem_error& e) {
        g_Log->Error("Failed to create mp4 directory: %s", e.what());
        return false;
    }

    std::string fileExtension = ".mp4";
    fs::path fullSavePath = savePath / (uuid + fileExtension);

    if (!spDownload->Save(fullSavePath.string())) {
        g_Log->Error("Failed to save downloaded dream to %s", fullSavePath.string().c_str());
        return false;
    }

    g_Log->Info("Successfully downloaded and saved dream to %s", fullSavePath.string().c_str());
    SetDownloadStatus("Download complete " + dream->name);
    
    Cache::CacheManager::DiskCachedItem newDiskItem;
    newDiskItem.uuid = uuid;
    newDiskItem.version = dream->video_timestamp;
    newDiskItem.downloadDate = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    cm.addDiskCachedItem(newDiskItem);
    cm.decreaseRemainingQuota(dream->size);
    
    if (enqueue) {
        // g_Player().Add(fullSavePath.string().c_str());
    }
    
    return true;
}

// This is used in instant download context, it's run asynchronously above
// Multiple actions are required to grab the link and download the file
// Then a callback will start playing the dream immediately on the correct thread
bool DreamDownloader::DownloadDreamNow(const std::string& uuid, std::function<void(bool, const std::string&)> callback) {
    g_Log->Info("Immediately downloading dream with UUID: %s", uuid.c_str());
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    if (!cm.hasDiskCachedItem(uuid.c_str())) {
        auto link = EDreamClient::GetDreamDownloadLink(uuid);
        
        if (!link.empty()) {
            g_Log->Info("Download link received: %s", link.c_str());
            bool success = DownloadDream(uuid, link, false);
            
            if (success) {
                g_Log->Info("Successfully immediately downloaded dream with UUID: %s", uuid.c_str());
            } else {
                g_Log->Info("Failed to immediately download dream with UUID: %s", uuid.c_str());
            }
            
            // Execute the callback if provided
            if (callback) {
                callback(true, uuid);
            }
            
            return success;
        } else {
            g_Log->Error("Download link denied");
        }
    }
    
    return false;
}


}
