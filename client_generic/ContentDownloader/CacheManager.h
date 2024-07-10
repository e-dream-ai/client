//
//  CacheManager.h
//  e-dream
//
//  Created by Guillaume Louel on 09/07/2024.
//

#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>

struct Dream {
    std::string uuid;
    std::string name;
    std::string artist;
    std::string size;
    std::string status;
    std::string fps;
    int frames;
    std::string thumbnail;
    int upvotes;
    int downvotes;
    bool nsfw;
    std::string frontendUrl;
    long long video_timestamp;
    long long timestamp;
};


class CacheManager {
public:
    // Delete copy constructor and assignment operator
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    // Get the singleton instance
    static CacheManager& getInstance();

    // Json loading
    void loadJsonFile(const std::string& filename);
    const std::unordered_map<std::string, Dream>& getDreams() const;
    
    // 
    bool isCached(std::string uuid);
    bool needsMetadata(std::string uuid, int timeStamp);
    bool fetchMetadata(std::string uuid);

private:
    // Private constructor
    CacheManager() = default;

    // The singleton instance
    static std::unique_ptr<CacheManager> instance;
    std::unordered_map<std::string, Dream> dreams;
};

#endif // CACHE_MANAGER_H
