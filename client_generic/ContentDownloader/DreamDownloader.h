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
#include <boost/thread.hpp>

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
            thread.join();
        }
    }
    
    void AddDreamUUID(const std::string& uuid);
    void AddDreamUUIDs(const std::vector<std::string>& uuids);
    size_t GetDreamUUIDCount() const;
    
    bool DownloadDream(const std::string& uuid, const std::string& downloadLink);


private:
    void FindDreamsThread();
    
    boost::thread thread;
    std::atomic<bool> isRunning;
    std::set<std::string> m_dreamUUIDs;
    mutable std::mutex m_mutex;
};

}



#endif /* DreamDownloader_h */
