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
#include "ProgressiveHttpStream.h"

namespace ContentDownloader
{

std::uintmax_t minDiskSpace = (std::uintmax_t)1024 * 1024 * 1024 * 5;

void DreamDownloader::FindDreamsToDownload() {
    
    if (isRunning.exchange(true)) {
        std::cout << "FindDreamsToDownload is already running." << std::endl;
        return;
    }
    m_stopping.store(false);

    thread = boost::thread(&DreamDownloader::FindDreamsThread, this);
    
}

std::optional<std::string> DreamDownloader::GetNextDreamToDownload() {
    // Get the next uncached dream from PlaylistManager
    auto& playlistMgr = g_Player().GetPlaylistManager();
    return playlistMgr.getNextUncachedDream();
}

bool DreamDownloader::IsDreamBeingDownloaded(const std::string& uuid) const {
    {
        std::lock_guard<std::mutex> lock(m_downloadingMutex);
        if (m_currentlyDownloading.has_value() && m_currentlyDownloading.value() == uuid)
            return true;
    }
    return Network::DownloadRegistry::Instance().Contains(uuid);
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

void DreamDownloader::SetDiskSpaceLow(bool low) {
    m_diskSpaceLow.store(low);
}

bool DreamDownloader::IsDiskSpaceLow() const {
    return m_diskSpaceLow.load();
}

void DreamDownloader::CheckDiskSpace() {
    auto& pathManager = Cache::PathManager::getInstance();
    auto freeSpace = Cache::CacheManager::getInstance().getFreeSpace(pathManager.mp4Path());
    g_Log->Info("Disk space check at startup: %llu bytes free", (unsigned long long)freeSpace);

    // Treat a failed storage init (couldn't create the content directories)
    // the same as low disk space: it almost always means the disk is full.
    if (!pathManager.storageReady() || freeSpace < minDiskSpace) {
        g_Log->Info("Not enough disk space at startup");
        SetDiskSpaceLow(true);
        SetDownloadStatus("Not enough disk space");
    } else {
        SetDiskSpaceLow(false);
    }
}

// MARK: Thread function
void DreamDownloader::FindDreamsThread() {
    PlatformUtils::SetThreadName("FindDreamsToDownload");
    
    try {
        // Grab the CacheManager
        Cache::CacheManager& cm = Cache::CacheManager::getInstance();

        // Set a default delay (30 seconds)
        // int delayTime = 30;
        
        // Minimum space in cache/quota to consider downloading (100 MB)
        std::uintmax_t minSpaceForDream = (std::uintmax_t)1024 * 1024 * 100;

        bool quotaWarningLogged  = false;
        bool allCachedWarningLogged = false;

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
 
            // Check quota status
            bool hasEnoughQuota = cm.hasQuotaToDownload();

            if (!hasEnoughQuota) {
                if (!quotaWarningLogged) {
                    g_Log->Info("Quota too low to grab new videos");
                    quotaWarningLogged = true;
                }
                SetDownloadStatus("Your quota is expired");
                break;
            } else {
                quotaWarningLogged = false;
            }
            
            // First check if there's a dream to download
            auto nextDream = GetNextDreamToDownload();
            if (!nextDream.has_value()) {
                if (!allCachedWarningLogged) {
                    auto& pm = g_Player().GetPlaylistManager();
                    g_Log->Warning(
                        "All %zu dreams in playlist \"%s\" are already cached — "
                        "nothing to download. If unexpected, check your server-side "
                        "playlist assignment (remaining quota: %.2f GB).",
                        pm.getPlaylistSize(),
                        pm.getPlaylistName().c_str(),
                        static_cast<double>(cm.getRemainingQuota()) / (1024.0 * 1024.0 * 1024.0));
                    allCachedWarningLogged = true;
                }
                SetDownloadStatus("All playlist dreams cached");
                break;
            }
            allCachedWarningLogged = false;
            
            // Preflight checks - only clean cache if we have something to download
            
            // Do we even have some disk space available ?
            if (cm.getFreeSpace(Cache::PathManager::getInstance().mp4Path()) < minDiskSpace) {
                g_Log->Info("Not enough disk space %ll", cm.getFreeSpace(Cache::PathManager::getInstance().mp4Path()));
                SetDownloadStatus("Not enough disk space");
                SetDiskSpaceLow(true);
                break;
            } else {
                SetDiskSpaceLow(false);
            }

            // Is our cache full ? Keep evicting until we have enough space
            bool cacheFull = false;
            while (cm.getRemainingCacheSpace() < minSpaceForDream) {
                // First try to remove the oldest video that's not part of the playlist
                if (!cm.removeOldestVideo(true)) {
                    // If that fails, try removing oldest from playlist too
                    if (!cm.removeOldestVideo(false)) {
                        g_Log->Info("Not enough space in cache remaining : %ju, cannot remove any video", cm.getRemainingCacheSpace());
                        SetDownloadStatus("Your cache is full");
                        cacheFull = true;
                        break;
                    }
                    g_Log->Info("Removed oldest dream from playlist to make space");
                }
            }
            if (cacheFull) break;
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

                const bool downloaded = ClaimAndDownload(current_uuid, /*playlistPrefetch=*/true);

                // Clear the currently downloading flag
                {
                    std::lock_guard<std::mutex> lock(m_downloadingMutex);
                    m_currentlyDownloading.reset();
                }

                if (!downloaded) {
                    // A transient network failure otherwise selects the same
                    // uncached successor again immediately and spins at thousands
                    // of requests per second (especially after shutdown aborts I/O).
                    if (!isRunning.load())
                        break;
                    boost::this_thread::sleep(boost::get_system_time() +
                                              boost::posix_time::seconds(1));
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

bool DreamDownloader::ClaimAndDownload(const std::string& uuid, bool playlistPrefetch) {
    auto& registry = Network::DownloadRegistry::Instance();
    // Claim before fetching the link so a Next landing meanwhile waits to
    // attach instead of starting its own download.
    if (!registry.Claim(uuid)) {
        g_Log->Info("Dream %s is already downloading, skipping", uuid.c_str());
        return false;
    }

    std::shared_ptr<Network::ProgressiveHttpStream> stream;
    // Release the entry on every exit, including thread interruption.
    struct Unclaim {
        const std::string& uuid;
        const std::shared_ptr<Network::ProgressiveHttpStream>& stream;
        ~Unclaim() { Network::DownloadRegistry::Instance().Remove(uuid, stream.get()); }
    } unclaim{uuid, stream};

    auto link = EDreamClient::GetDreamDownloadLink(uuid);
    if (link.empty()) {
        g_Log->Error("Download link denied");
        return false;
    }
    g_Log->Info("Download link received: %s", link.c_str());
    SetDownloadStatus("");

    return DownloadDream(uuid, link, stream, playlistPrefetch);
}

bool DreamDownloader::DownloadDream(const std::string& uuid, const std::string& downloadLink,
                                    std::shared_ptr<Network::ProgressiveHttpStream>& stream,
                                    bool playlistPrefetch) {
    // Grab the CacheManager
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    fs::path savePath = Cache::PathManager::getInstance().mp4Path();

    if (uuid.empty() || downloadLink.empty() || savePath.empty()) {
        g_Log->Error("Invalid input parameters for DownloadDream");
        return false;
    }

    auto dream = cm.getDream(uuid);
    if (!dream) {
        g_Log->Error("No metadata for dream %s, cannot download", uuid.c_str());
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

    // Download into .tmp first; the player may read it while it fills.
    fs::path tmpPath = savePath / (uuid + ".tmp");
    fs::path finalPath = savePath / (uuid + ".mp4");

    stream = std::make_shared<Network::ProgressiveHttpStream>();
    if (!stream->Start(downloadLink, tmpPath.string(),
                       /*downloadAbort=*/[this]() { return m_stopping.load(); })) {
        g_Log->Error("Failed to start download of dream %s", uuid.c_str());
        stream.reset();
        return false;
    }
    Network::DownloadRegistry::Instance().Publish(uuid, stream);

    const std::string progressName = "Downloading dream: " + dream->name;
    bool committed = false;
    // Unless the file reached the cache, stop the transfer (failing any
    // attached reader) and drop the partial file.
    struct Cleanup {
        const std::string& progressName;
        Network::ProgressiveHttpStream& stream;
        const fs::path& tmpPath;
        const bool& committed;
        ~Cleanup() {
            g_NetworkManager->Remove(progressName);
            if (!committed) {
                stream.Stop();
                std::error_code ec;
                fs::remove(tmpPath, ec);
            }
        }
    } cleanup{progressName, *stream, tmpPath, committed};

    auto& playlistMgr = g_Player().GetPlaylistManager();
    while (!stream->Done()) {
        if (playlistPrefetch && !playlistMgr.containsDream(uuid)) {
            // Unpublish first so no clip can attach while we decide.
            Network::DownloadRegistry::Instance().Remove(uuid, stream.get());
            if (stream->Readers() == 0) {
                g_Log->Info("Cancelling prefetch of %s: no longer in the playlist", uuid.c_str());
                SetDownloadStatus("");
                return false;
            }
        }

        const int64_t size = stream->Size();
        const int64_t got = stream->BytesDownloaded();
        g_NetworkManager->UpdateProgress(progressName, "Active",
                                         size > 0 ? 100.0 * got / size : 0.0,
                                         static_cast<double>(got));
        boost::this_thread::sleep(boost::get_system_time() +
                                  boost::posix_time::milliseconds(250));
    }

    if (!stream->Finished()) {
        SetDownloadStatus("Download failed for " + dream->name);
        g_Log->Error("Failed to download dream %s", uuid.c_str());
        return false;
    }

    // If we have an MD5 hash in the metadata, verify it
    if (!dream->md5.empty()) {
        std::string downloadedMd5 = PlatformUtils::CalculateFileMD5(tmpPath.string());
        if (downloadedMd5 != dream->md5) {
            g_Log->Error("md5 mismatch for %s. Expected: %s, Got: %s",
                         uuid.c_str(), dream->md5.c_str(), downloadedMd5.c_str());
            EDreamClient::ReportMD5Failure(uuid, downloadedMd5, false);
            return false;
        }
        // TODO : remove that from log at some point
        g_Log->Info(std::string("md5 match ") + dream->md5);
    } else {
        // TODO : remove that from log at some point
        g_Log->Info("No md5 in metadata");
    }

    // Rename tmp to mp4; attached readers carry on from the new path
    if (!stream->CommitTo(finalPath.string())) {
        g_Log->Error("Failed to rename tmp file to mp4 for %s", uuid.c_str());
        return false;
    }
    committed = true;

    g_Log->Info("Successfully downloaded and saved dream to %s", finalPath.string().c_str());
    SetDownloadStatus("");
    
    Cache::CacheManager::DiskCachedItem newDiskItem;
    newDiskItem.uuid = uuid;
    newDiskItem.version = dream->video_timestamp;
    newDiskItem.downloadDate = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    newDiskItem.md5 = dream->md5;

    cm.addDiskCachedItem(newDiskItem);
    cm.decreaseRemainingQuota(dream->size);
    
    return true;
}

// This is used in instant download context, it's run asynchronously above
// Multiple actions are required to grab the link and download the file
// Then a callback will start playing the dream immediately on the correct thread
bool DreamDownloader::DownloadDreamNow(const std::string& uuid, std::function<void(bool, const std::string&)> callback) {
    g_Log->Info("Immediately downloading dream with UUID: %s", uuid.c_str());
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();

    if (!cm.hasDiskCachedItem(uuid.c_str())) {
        // Evict old videos if cache is full
        std::uintmax_t minSpaceForDream = (std::uintmax_t)1024 * 1024 * 100;
        while (cm.getRemainingCacheSpace() < minSpaceForDream) {
            if (!cm.removeOldestVideo(true)) {
                if (!cm.removeOldestVideo(false)) {
                    g_Log->Info("DownloadDreamNow: cache full, cannot make space");
                    break;
                }
            }
        }

        bool success = ClaimAndDownload(uuid, /*playlistPrefetch=*/false);

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
    }
    
    return false;
}


}
