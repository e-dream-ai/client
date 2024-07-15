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
    while (isRunning.load()) {
        g_Log->Info("Searching for dreams to download...");

        // We look through the list of uuids we've been given
        while (true) {
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

        // TODO : check vcpkg for missing boost::chrono? that would be cleaner
        //boost::this_thread::sleep_for(boost::chrono::seconds(30));

    }
    g_Log->Info("Exiting FindDreamsThreads()");
}


}
