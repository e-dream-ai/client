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
#include "PlaylistManager.h"

namespace ContentDownloader
{

void DreamDownloader::FindDreamsToDownload() {
    
    if (isRunning.exchange(true)) {
        std::cout << "FindDreamsToDownload is already running." << std::endl;
        return;
    }

    thread = boost::thread(&DreamDownloader::FindDreamsThread, this);
    
}

std::optional<std::string> DreamDownloader::GetNextDreamToDownload() {
    // Get the next uncached dream from PlaylistManager
    auto& playlistMgr = g_Player().GetPlaylistManager();
    return playlistMgr.getNextUncachedDream();
}

bool DreamDownloader::IsDreamBeingDownloaded(const std::string& uuid) const {
    std::lock_guard<std::mutex> lock(m_downloadingMutex);
    return m_currentlyDownloading.has_value() && m_currentlyDownloading.value() == uuid;
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
    
    try {
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
        
        
        
        while (isRunning.load()) {
            // Check for thread interruption
            boost::this_thread::interruption_point();
            
            // Preflight checklist
 
            // Make sure we have some remaining quota
            if (cm.getRemainingQuota() < (long long)minSpaceForDream) {
                g_Log->Info("Quota too low to grab new videos %ll", cm.getRemainingQuota());
                // TODO : some user funnel to add quota, later on
                SetDownloadStatus("Your quota is expired");

                break;
            }
            
            // First check if there's a dream to download
            auto nextDream = GetNextDreamToDownload();
            if (!nextDream.has_value()) {
                // No more uncached dreams to download
                break;
            }
            
            // Preflight checks - only clean cache if we have something to download
            
            // Do we even have some disk space available ?
            if (cm.getFreeSpace(Cache::PathManager::getInstance().mp4Path()) < minDiskSpace) {
                g_Log->Info("Not enough disk space %ll", cm.getFreeSpace(Cache::PathManager::getInstance().mp4Path()));
                SetDownloadStatus("Not enough disk space");
                break;
            }

            // Is our cache full ?
            if (cm.getRemainingCacheSpace() < minSpaceForDream) {
                // First try to remove the oldest video that's not part of the playlist
                if (!cm.removeOldestVideo(true)) {
                    // If that fails, try removing oldest from playlist too
                    if (!cm.removeOldestVideo(false)) {
                        g_Log->Info("Not enough space in cache remaining : %ju, cannot remove any video", cm.getRemainingCacheSpace());
                        SetDownloadStatus("Your cache is full");
                        break;
                    }
                    g_Log->Info("Removed oldest dream from playlist to make space");
                }
            }
            // /Preflight
            
            std::string current_uuid = nextDream.value();
            
            // Mark this dream as being downloaded
            {
                std::lock_guard<std::mutex> lock(m_downloadingMutex);
                m_currentlyDownloading = current_uuid;
            }
            
            g_Log->Info("Processing dream with UUID: %s", current_uuid.c_str());
            if (!cm.hasDiskCachedItem(current_uuid.c_str())) {
                // Check for interruption before making network call
                boost::this_thread::interruption_point();
                
                auto link = EDreamClient::GetDreamDownloadLink(current_uuid);
                
                if (!link.empty()) {
                    g_Log->Info("Download link received: %s", link.c_str());
                    SetDownloadStatus("Downloading dream...");

                    DownloadDream(current_uuid, link);
                    
                    // Clear the currently downloading flag
                    {
                        std::lock_guard<std::mutex> lock(m_downloadingMutex);
                        m_currentlyDownloading.reset();
                    }
                    
                } else {
                    g_Log->Error("Download link denied");
                    // Clear the currently downloading flag on error
                    {
                        std::lock_guard<std::mutex> lock(m_downloadingMutex);
                        m_currentlyDownloading.reset();
                    }
                }

            }
            
        }

        //SetDownloadStatus("No dream to download");
        // Sleep for a while before the next iteration
        boost::this_thread::sleep(boost::get_system_time() +
                             boost::posix_time::seconds(10));
    }
    
    } catch (boost::thread_interrupted&) {
        g_Log->Info("FindDreamsThread interrupted");
    }
    
    // Clear any currently downloading flag when exiting
    {
        std::lock_guard<std::mutex> lock(m_downloadingMutex);
        m_currentlyDownloading.reset();
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

    // Save with .tmp extension first
    fs::path tmpPath = savePath / (uuid + ".tmp");
    fs::path finalPath = savePath / (uuid + ".mp4");

    if (!spDownload->Save(tmpPath.string())) {
        g_Log->Error("Failed to save downloaded dream to %s", tmpPath.string().c_str());
        return false;
    }

    // If we have an MD5 hash in the metadata, verify it
    if (!dream->md5.empty()) {
        std::string downloadedMd5 = PlatformUtils::CalculateFileMD5(tmpPath.string());
        if (downloadedMd5 != dream->md5) {
            g_Log->Error("md5 mismatch for %s. Expected: %s, Got: %s",
                         uuid.c_str(), dream->md5.c_str(), downloadedMd5.c_str());
            EDreamClient::ReportMD5Failure(uuid, downloadedMd5, false);
            fs::remove(tmpPath);
            return false;
        }
        // TODO : remove that from log at some point
        g_Log->Info(std::string("md5 match ") + dream->md5);
    } else {
        // TODO : remove that from log at some point
        g_Log->Info("No md5 in metadata");
    }

    // Rename tmp to mp4
    try {
        fs::rename(tmpPath, finalPath);
    } catch (const fs::filesystem_error& e) {
        g_Log->Error("Failed to rename tmp file to mp4: %s", e.what());
        return false;
    }

    g_Log->Info("Successfully downloaded and saved dream to %s", finalPath.string().c_str());
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
