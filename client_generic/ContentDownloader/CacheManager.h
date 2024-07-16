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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

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
    struct DiskCachedItem {
        std::string uuid;
        long long version;
        long long downloadDate;
    };

    struct HistoryItem {
        std::string uuid;
        long long version;
        long long downloadDate;
        long long deletedDate;
    };
    
    // Delete copy constructor and assignment operator
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;
    
    // Get the singleton instance
    static CacheManager& getInstance();
    
    bool hasDream(const std::string& uuid) const;
    const Dream* getDream(const std::string& uuid) const;
    int dreamCount() const;
    
    // Individual json loading
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

    
    // diskCachedItem/historyItem
    void addDiskCachedItem(const DiskCachedItem& item);
    void removeDiskCachedItem(const std::string& uuid);
    void addHistoryItem(const HistoryItem& item);

    void saveDiskCachedToJson() const;
    void saveHistoryToJson() const;

    void loadDiskCachedFromJson();
    void loadHistoryFromJson();
    
private:
    // Private constructor
    CacheManager() = default;
    
    // The singleton instance
    static std::unique_ptr<CacheManager> instance;
    std::unordered_map<std::string, Dream> dreams;
    static long long remainingQuota;
    
    // diskCachedItem/historyItem
    std::vector<DiskCachedItem> diskCached;
    std::vector<HistoryItem> history;

    boost::property_tree::ptree serializeDiskCachedItem(const DiskCachedItem& item) const;
    boost::property_tree::ptree serializeHistoryItem(const HistoryItem& item) const;

    DiskCachedItem deserializeDiskCachedItem(const boost::property_tree::ptree& pt) const;
    HistoryItem deserializeHistoryItem(const boost::property_tree::ptree& pt) const;
};

} // Namespace

#endif // CACHE_MANAGER_H
