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

namespace Cache {

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
    float activityLevel = 1.f;
};


class CacheManager {
public:
    // Delete copy constructor and assignment operator
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;
    
    // Get the singleton instance
    static CacheManager& getInstance();
    
    bool hasDream(const std::string& uuid) const;
    const Dream* getDream(const std::string& uuid) const;
    int dreamCount() const;
    
    // Json loading
    void loadCachedMetadata();
    
    void loadJsonFile(const std::string& filename);
    const std::unordered_map<std::string, Dream>& getDreams() const;
    
    //
    bool areMetadataCached(std::string uuid);
    bool needsMetadata(std::string uuid, long long timeStamp);
    void reloadMetadata(std::string uuid);
    
    // Quota getter/setter
    long long getRemainingQuota() const {
       return remainingQuota;
    }

    void setRemainingQuota(long long newQuota) {
       remainingQuota = newQuota;
    }
    
    // Used space by a path
    std::uintmax_t getUsedSpace(const char* path);
    // Underlying disk free space to that path
    std::uintmax_t getFreeSpace(const char* path);
    // Computed remaining cache space base on user settings
    std::uintmax_t getRemainingCacheSpace();

private:
    // Private constructor
    CacheManager() = default;
    
    // The singleton instance
    static std::unique_ptr<CacheManager> instance;
    std::unordered_map<std::string, Dream> dreams;
    static long long remainingQuota;
};

} // Namespace

#endif // CACHE_MANAGER_H
