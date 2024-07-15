//
//  DreamDownloader.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 15/07/2024.
//

#include "DreamDownloader.h"
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

// THREAD CODE
void DreamDownloader::FindDreamsThread() {
    while (isRunning.load()) {
        g_Log->Info("Searching for dreams to download...");
        



        // Sleep for a while before the next iteration
        //boost::this_thread::sleep_for(boost::chrono::seconds(30));
        boost::this_thread::sleep(boost::get_system_time() +
                             boost::posix_time::seconds(30));
    }
}


}
