//
//  PathManager.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 7/30/24.
//

#include "PathManager.h"
#include "Settings.h"
#include <stdexcept>

namespace Cache {

PathManager& PathManager::getInstance() {
    static PathManager instance;
    return instance;
}

PathManager::PathManager() {
    initializePaths();
}

const std::filesystem::path& PathManager::rootPath() const {
    return m_rootPath;
}

const std::filesystem::path& PathManager::mp4Path() const {
    return m_mp4Path;
}

const std::filesystem::path& PathManager::jsonDreamPath() const {
    return m_jsonDreamPath;
}

const std::filesystem::path& PathManager::jsonPlaylistPath() const {
    return m_jsonPlaylistPath;
}

void PathManager::initializePaths() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string root = g_Settings()->Get("settings.content.sheepdir",
                                         g_Settings()->Root() + "content");

    m_rootPath = std::filesystem::path(root).make_preferred();
    m_mp4Path = m_rootPath / "mp4";
    m_jsonDreamPath = m_rootPath / "json" / "dream";
    m_jsonPlaylistPath = m_rootPath / "json" / "playlist";

    // Create directories
    std::filesystem::create_directories(m_rootPath);
    std::filesystem::create_directories(m_mp4Path);
    std::filesystem::create_directories(m_jsonDreamPath);
    std::filesystem::create_directories(m_jsonPlaylistPath);
}

} // namespace Cache
