//
//  CacheManager.h
//  e-dream
//
//  Created by Guillaume Louel on 09/07/2024.
//

#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/json.hpp>

#include "PathManager.h"
#include "Dream.h"

namespace Cache {

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
    std::string getDreamPath(const std::string& uuid) const;

    void cacheAndPlayImmediately(const std::string& uuid);
    
    int dreamCount() const;
    
    // Individual json loading
    void loadCachedMetadata();
    
    void loadJsonFile(const std::string& filename);
    const std::unordered_map<std::string, Dream>& getDreams() const;
    
    //
    bool areMetadataCached(std::string uuid);
    bool needsMetadata(std::string uuid, long long timeStamp);
    void reloadMetadata(std::string uuid);
    bool deleteMetadata(const std::string& uuid);
    
    // Quota getter/setter
    long long getRemainingQuota() const {
       return remainingQuota;
    }
    
    std::string getRemainingQuotaAsString() const {
        const double GB = 1024.0 * 1024.0 * 1024.0;
        double remainingGB = static_cast<double>(remainingQuota) / GB;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << remainingGB << " GB";
        return oss.str();
    }
    
    void setRemainingQuota(long long newQuota) {
       remainingQuota = newQuota;
    }
    void decreaseRemainingQuota(long long amount);
    
    // Used space by a path
    std::uintmax_t getUsedSpace(const std::filesystem::path& path) const;
    // Underlying disk free space to that path
    std::uintmax_t getFreeSpace(const std::filesystem::path& path) const;
    // Computed remaining cache space base on user settings
    std::uintmax_t getRemainingCacheSpace();
    double getCacheSize() const;
    
    // diskCachedItem/historyItem
    void addDiskCachedItem(const DiskCachedItem& item);
    void removeDiskCachedItem(const std::string& uuid);
    void addHistoryItem(const HistoryItem& item);

    bool hasDiskCachedItem(const std::string& uuid) const;
    
    void saveDiskCachedToJson() const;
    void saveHistoryToJson() const;

    void loadDiskCachedFromJson();
    void loadHistoryFromJson();
    
    void cleanupDiskCache();
    void removeUnknownVideos();
    bool deleteDream(const std::string& uuid);
    
    size_t getCachedDreamCount() const {
        return diskCached.size();
    }
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
    
    // JSON Parser helpers
    std::string safe_get_string(const boost::json::object& obj, const char* key, const std::string& default_value = "") {
        auto it = obj.find(key);
        if (it != obj.end() && it->value().is_string()) {
            return it->value().as_string().c_str();
        }
        return default_value;
    }

    int64_t safe_get_int64(const boost::json::object& obj, const char* key, int64_t default_value = 0) {
        auto it = obj.find(key);
        if (it != obj.end() && it->value().is_int64()) {
            return it->value().as_int64();
        }
        return default_value;
    }

    bool safe_get_bool(const boost::json::object& obj, const char* key, bool default_value = false) {
        auto it = obj.find(key);
        if (it != obj.end() && it->value().is_bool()) {
            return it->value().as_bool();
        }
        return default_value;
    }

    float safe_get_float(const boost::json::object& obj, const char* key, float default_value = 1.0f) {
        auto it = obj.find(key);
        if (it != obj.end() && it->value().is_number()) {
            return it->value().to_number<float>();
        }
        return default_value;
    }
    
};

// MARK: Extra dream functions that require CacheManager
inline std::string Dream::getCachedPath() const {
    const CacheManager& cacheManager = CacheManager::getInstance();
    
    if (!cacheManager.hasDiskCachedItem(uuid)) {
        return "";
    }
    
    std::filesystem::path folderPath = PathManager::getInstance().mp4Path();
    std::filesystem::path filePath = folderPath / (uuid + ".mp4");
    
    return filePath.string();
}

inline bool Dream::isCached() const {
    const CacheManager& cacheManager = CacheManager::getInstance();
    return cacheManager.hasDiskCachedItem(uuid);
}


} // Namespace

#endif // CACHE_MANAGER_H
