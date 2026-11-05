//
//  PathManager.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 7/30/24.
//

#include "PathManager.h"
#include "Settings.h"
#include "Log.h"
#include <stdexcept>
#include <system_error>

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

bool PathManager::storageReady() const {
    return m_storageReady;
}

void PathManager::initializePaths() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string root = g_Settings()->Get("settings.content.sheepdir",
                                         g_Settings()->Root() + "content");

    m_rootPath = std::filesystem::path(root).make_preferred();
    m_mp4Path = m_rootPath / "mp4";
    m_jsonDreamPath = m_rootPath / "json" / "dream";
    m_jsonPlaylistPath = m_rootPath / "json" / "playlist";

    // Create directories. Use the non-throwing overload: on a full or
    // unwritable disk create_directories would otherwise throw an uncaught
    // filesystem_error and abort the app at launch (issue #664). Instead we
    // record the failure so callers can disable content I/O and report it.
    const std::filesystem::path* dirs[] = {
        &m_rootPath, &m_mp4Path, &m_jsonDreamPath, &m_jsonPlaylistPath
    };

    m_storageReady = true;
    for (const auto* dir : dirs) {
        std::error_code ec;
        std::filesystem::create_directories(*dir, ec);
        // create_directories returns false (no error) when the directory
        // already exists; only a non-empty error_code is an actual failure.
        if (ec) {
            m_storageReady = false;
            g_Log->Error("Failed to create directory %s: %s",
                         dir->string().c_str(), ec.message().c_str());
        }
    }

    if (!m_storageReady) {
        g_Log->Error("Storage initialization failed (disk full or unwritable). "
                     "Content downloads will be disabled.");
    }
}

} // namespace Cache
