//
//  CacheManager.h
//  e-dream
//
//  Created by Guillaume Louel on 09/07/2024.
//

#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <chrono>
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
        std::string md5;  // Empty on legacy cache entries until backfilled
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
    // Returns a shared owning handle so callers keep the Dream alive even if the
    // cache mutates (or the entry is replaced) on another thread. Returns nullptr
    // when the dream isn't cached.
    std::shared_ptr<const Dream> getDream(const std::string& uuid) const;
    std::string getDreamPath(const std::string& uuid) const;

    std::vector<Dream> getAllCachedDreams() const;
    
    void cacheAndPlayImmediately(const std::string& uuid);
    
    int dreamCount() const;
    
    // Individual json loading
    void loadCachedMetadata();
    
    void loadJsonFile(const std::string& filename);

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
    
    // Quota expiration getter/setter
    std::chrono::system_clock::time_point getQuotaExpiresAt() const {
        return quotaExpiresAt;
    }
    
    void setQuotaExpiresAt(const std::chrono::system_clock::time_point& expiresAt) {
        quotaExpiresAt = expiresAt;
    }
    
    // Check if quota expires within the specified duration
    bool quotaExpiresWithin(const std::chrono::hours& duration) const {
        auto now = std::chrono::system_clock::now();
        return quotaExpiresAt > now && quotaExpiresAt <= now + duration;
    }
    
    // Used space by a path
    std::uintmax_t getUsedSpace(const std::filesystem::path& path) const;
    // Underlying disk free space to that path
    std::uintmax_t getFreeSpace(const std::filesystem::path& path) const;
    // Computed remaining cache space base on user settings
    std::uintmax_t getRemainingCacheSpace();
    double getCacheSize() const;
    
    bool isPlaylistFillingCache(const std::vector<std::string>& playlistUUIDs);

    
    // diskCachedItem/historyItem
    void addDiskCachedItem(const DiskCachedItem& item);
    void removeDiskCachedItem(const std::string& uuid);
    void addHistoryItem(const HistoryItem& item);

    bool hasDiskCachedItem(const std::string& uuid) const;
    std::optional<DiskCachedItem> getDiskCachedItem(const std::string& uuid) const;

    // Compare server dream md5 to the disk-cache entry; invalidate on mismatch.
    // Legacy entries with empty stored md5 adopt the metadata md5 when their
    // video_timestamp is unchanged, and are hashed on a background worker if not.
    void validateCachedVideo(const std::string& uuid);
    // validateCachedVideo for every disk-cache entry not in skipUuids.
    void validateAllCachedVideos(const std::unordered_set<std::string>& skipUuids);

    // Drop the cached MP4 + disk entry but keep metadata so it can re-download.
    // Defers while the UUID is open for decode (Windows file locks).
    bool invalidateVideo(const std::string& uuid);

    // Retry deferred invalidations except UUIDs still open for decode.
    void flushPendingVideoInvalidations(const std::unordered_set<std::string>& stillOpenUuids);
    
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
    
    // Methods for managing evicted UUIDs
    void addEvictedUUID(const std::string& uuid);
    bool isUUIDEvicted(const std::string& uuid) const;
    const std::vector<std::string>& getEvictedUUIDs() const;
    void clearEvictedUUIDs();
    size_t getEvictedUUIDsCount() const {
        return m_evictedUUIDs.size();
    }
    
    // Save and load evicted UUIDs
    void saveEvictedUUIDsToJson() const;
    void loadEvictedUUIDsFromJson();
    
    bool removeOldestVideo(bool respectPlaylist = true);
    void resizeCache(std::uintmax_t targetSize);

private:
    // Private constructor
    CacheManager() = default;
    
    // The singleton instance
    static std::unique_ptr<CacheManager> instance;
    std::unordered_map<std::string, std::shared_ptr<Dream>> dreams;
    // Guards all access to `dreams`. It is read from playback/preflight threads
    // and written from metadata-load threads concurrently, so every access must
    // hold this lock. Stored values are shared_ptr so getDream() can hand out an
    // owning handle that outlives any concurrent mutation of the map.
    mutable std::shared_mutex dreamsMutex;
    static long long remainingQuota;
    std::chrono::system_clock::time_point quotaExpiresAt;
    
    // diskCachedItem/historyItem
    std::vector<DiskCachedItem> diskCached;
    std::vector<HistoryItem> history;

    std::vector<std::string> m_evictedUUIDs;

    // UUIDs waiting for decode to finish before MP4 delete (Windows locks).
    std::unordered_set<std::string> m_pendingVideoInvalidations;
    mutable std::mutex m_pendingVideoInvalidationsMutex;

    // Legacy entries waiting to be hashed by the single backfill worker.
    std::deque<std::string> m_md5BackfillQueue;
    std::unordered_set<std::string> m_md5BackfillQueued;
    bool m_md5BackfillRunning = false;
    std::mutex m_md5BackfillMutex;
    
    // Mutex to protect diskCached container from concurrent access
    mutable std::shared_mutex diskCachedMutex;

    boost::property_tree::ptree serializeDiskCachedItem(const DiskCachedItem& item) const;
    boost::property_tree::ptree serializeHistoryItem(const HistoryItem& item) const;
    boost::property_tree::ptree serializeEvictedUUIDs() const;

    DiskCachedItem deserializeDiskCachedItem(const boost::property_tree::ptree& pt) const;
    HistoryItem deserializeHistoryItem(const boost::property_tree::ptree& pt) const;
    void deserializeEvictedUUIDs(const boost::property_tree::ptree& pt);

    std::uintmax_t getVideoFileSize(const std::string& uuid) const;

    bool invalidateVideoNow(const std::string& uuid);
    void updateDiskCachedItemMd5(const std::string& uuid, const std::string& md5);
    void enqueueMd5Backfill(const std::string& uuid);
    void runMd5Backfill();
    
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
