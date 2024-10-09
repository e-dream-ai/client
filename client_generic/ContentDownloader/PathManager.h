//
//  PathManager.hpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 7/30/24.
//

#ifndef PATH_MANAGER_H
#define PATH_MANAGER_H

#include <string>
#include <filesystem>
#include <mutex>

namespace Cache {

class PathManager {
public:
    static PathManager& getInstance();

    const std::filesystem::path& rootPath() const;
    const std::filesystem::path& mp4Path() const;
    const std::filesystem::path& jsonDreamPath() const;
    const std::filesystem::path& jsonPlaylistPath() const;

private:
    PathManager();
    PathManager(const PathManager&) = delete;
    PathManager& operator=(const PathManager&) = delete;

    void initializePaths();

    mutable std::mutex m_mutex;
    std::filesystem::path m_rootPath;
    std::filesystem::path m_mp4Path;
    std::filesystem::path m_jsonDreamPath;
    std::filesystem::path m_jsonPlaylistPath;
};

} // namespace Cache

#endif // PATH_MANAGER_H
