//
//  DreamDownloader.h
//  ScreenSaver
//
//  Created by Guillaume Louel on 15/07/2024.
//

#ifndef DreamDownloader_h
#define DreamDownloader_h

#include <stdio.h>
#include <vector>
#include <string>
#include <set>
#include <mutex>
#include <future>
#include <boost/thread.hpp>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace Network
{
class ProgressiveHttpStream;
}

namespace ContentDownloader
{

class DreamDownloader {
public:
    DreamDownloader() : isRunning(false) {}
    ~DreamDownloader() {
        StopFindingDreams();
    }

    void FindDreamsToDownload(); 

    void StopFindingDreams()  {
        m_stopping.store(true);
        isRunning.store(false);
        if (thread.joinable()) {
            thread.interrupt();
            thread.join();
        }
    }
    
    // Get the next dream to download from PlaylistManager
    std::optional<std::string> GetNextDreamToDownload();
    
    // Check if a dream is currently being downloaded, by us or by the player
    bool IsDreamBeingDownloaded(const std::string& uuid) const; 
    
    // Disk space warning
    void SetDiskSpaceLow(bool low);
    bool IsDiskSpaceLow() const;
    void CheckDiskSpace();  // Check disk space immediately (call at startup)
    
    std::future<bool> DownloadImmediately(const std::string& uuid, std::function<void(bool, const std::string&)> callback = nullptr);

    bool DownloadDreamNow(const std::string& uuid, std::function<void(bool, const std::string&)> callback = nullptr);

    void SetDownloadStatus(const std::string& status);
    std::string GetDownloadStatus() const;

private:
    void FindDreamsThread();

    // Download uuid unless someone already is; shared with the player via
    // Network::DownloadRegistry so Next on it never starts a second copy.
    // playlistPrefetch: cancel if the dream leaves the playlist (e.g. a
    // playlist switch) while no clip is reading it.
    bool ClaimAndDownload(const std::string& uuid, bool playlistPrefetch);
    bool DownloadDream(const std::string& uuid, const std::string& downloadLink,
                       std::shared_ptr<Network::ProgressiveHttpStream>& stream,
                       bool playlistPrefetch);
    std::atomic<bool> m_stopping{false};
    
    boost::thread thread;
    std::atomic<bool> isRunning;
    
    // Track currently downloading dream
    mutable std::mutex m_downloadingMutex;
    std::optional<std::string> m_currentlyDownloading;

    // Download status
    mutable std::mutex m_statusMutex;
    std::string m_downloadStatus;
    
    // Disk space warning flag
    std::atomic<bool> m_diskSpaceLow{false};

};

}



#endif /* DreamDownloader_h */
