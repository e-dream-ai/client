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
        isRunning.store(false);
        if (thread.joinable()) {
            thread.interrupt();
            thread.join();
        }
    }
    
    // Get the next dream to download from PlaylistManager
    std::optional<std::string> GetNextDreamToDownload();
    
    // Check if a dream is currently being downloaded
    bool IsDreamBeingDownloaded(const std::string& uuid) const; 
    
    std::future<bool> DownloadImmediately(const std::string& uuid, std::function<void(bool, const std::string&)> callback = nullptr);

    bool DownloadDream(const std::string& uuid, const std::string& downloadLink, bool enqueue = true);
    bool DownloadDreamNow(const std::string& uuid, std::function<void(bool, const std::string&)> callback = nullptr);

    void SetDownloadStatus(const std::string& status);
    std::string GetDownloadStatus() const;
    
private:
    void FindDreamsThread();
    
    boost::thread thread;
    std::atomic<bool> isRunning;
    
    // Track currently downloading dream
    mutable std::mutex m_downloadingMutex;
    std::optional<std::string> m_currentlyDownloading;

    // Download status
    mutable std::mutex m_statusMutex;
    std::string m_downloadStatus;
};

}



#endif /* DreamDownloader_h */
