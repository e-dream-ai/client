//
//  DreamDownloader.h
//  ScreenSaver
//
//  Created by Guillaume Louel on 15/07/2024.
//

#ifndef DreamDownloader_h
#define DreamDownloader_h

#include <stdio.h>
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
    
    


private:
    void FindDreamsThread();
    
    boost::thread thread;
    std::atomic<bool> isRunning;
};

}



#endif /* DreamDownloader_h */
