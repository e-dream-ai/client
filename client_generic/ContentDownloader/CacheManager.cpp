//
//  CacheManager.cpp
//  e-dream
//
//  Created by Guillaume Louel on 09/07/2024.
//
//  This class manages the local cache and makes sure we have the correct associated metadata

#include "CacheManager.h"
#include <iostream>
#include <mutex>
#include <boost/json.hpp>

#include "PathManager.h"
#include "StringFormat.h"
#include "EDreamClient.h"
#include "ContentDownloader.h"
#include "Settings.h"
#include "Player.h"
#include "Log.h"

namespace Cache {

using boost::filesystem::exists;
namespace fs = std::filesystem;

std::unique_ptr<CacheManager> CacheManager::instance;
long long CacheManager::remainingQuota = 0;

CacheManager& CacheManager::getInstance() {
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new CacheManager()); });
    return *instance;
}

bool CacheManager::hasDream(const std::string& uuid) const {
    return dreams.find(uuid) != dreams.end();
}

const Dream* CacheManager::getDream(const std::string& uuid) const {
    auto it = dreams.find(uuid);
    if (it != dreams.end()) {
        return &(it->second);
    }
    return nullptr;
}

std::string CacheManager::getDreamPath(const std::string& uuid) const {
    if (!hasDiskCachedItem(uuid)) {
        return "";
    }
    
    fs::path folderPath = PathManager::getInstance().mp4Path();
    fs::path filePath = folderPath / (uuid + ".mp4");
    
    return filePath.string();
}

int CacheManager::dreamCount() const {
    return (int)dreams.size();
}


void CacheManager::loadJsonFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        g_Log->Error("Failed to open file: %s", filename.c_str());
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    boost::system::error_code ec;
    boost::json::value jv = boost::json::parse(content, ec);
    if (ec) {
        g_Log->Error("Failed to parse JSON: %s", ec.message().c_str());
        return;
    }

    if (jv.as_object().at("success").as_bool() && jv.as_object().at("data").as_object().contains("dreams")) {
        const auto& dreams_array = jv.as_object().at("data").as_object().at("dreams").as_array();
        for (const auto& dream_json : dreams_array) {
            if (!dream_json.is_object()) {
                g_Log->Warning("Skipping invalid dream entry: not an object");
                continue;
            }

            const auto& dream_obj = dream_json.as_object();
            
            Dream dream;
            
            dream.uuid = safe_get_string(dream_obj, "uuid");
            if (dream.uuid.empty()) {
                g_Log->Warning("Skipping dream with empty UUID");
                continue;
            }
            
            dream.name = safe_get_string(dream_obj, "name");
            dream.artist = safe_get_string(dream_obj, "artist");

            auto sizeString = safe_get_string(dream_obj, "size");
            std::istringstream iss(sizeString);
            if (!(iss >> dream.size)) {
                g_Log->Warning("Couldn't parse dream size");
                dream.size = 0;
            }
            dream.status = safe_get_string(dream_obj, "status");
            dream.fps = safe_get_string(dream_obj, "fps");
            dream.frames = safe_get_int64(dream_obj, "frames");
            dream.thumbnail = safe_get_string(dream_obj, "thumbnail");
            dream.upvotes = safe_get_int64(dream_obj, "upvotes");
            dream.downvotes = safe_get_int64(dream_obj, "downvotes");
            dream.nsfw = safe_get_bool(dream_obj, "nsfw");
            dream.frontendUrl = safe_get_string(dream_obj, "frontendUrl");
            dream.video_timestamp = safe_get_int64(dream_obj, "video_timestamp");
            dream.timestamp = safe_get_int64(dream_obj, "timestamp");
            dream.activityLevel = safe_get_float(dream_obj, "activityLevel");
            
            dreams[dream.uuid] = dream;
        }
    }
}

void CacheManager::cleanupDiskCache() {
    std::vector<DiskCachedItem> itemsToRemove;
    
    fs::path folderPath = PathManager::getInstance().mp4Path();
    
    // Look for files that may have been deleted
    for (const auto& item : diskCached) {
        fs::path filePath = folderPath / (item.uuid + ".mp4");
        
        if (!fs::exists(filePath)) {
            itemsToRemove.push_back(item);
        }
    }
    
    for (const auto& item : itemsToRemove) {
        // Remove from diskCached
        auto it = std::find_if(diskCached.begin(), diskCached.end(),
                               [&item](const DiskCachedItem& cachedItem) {
            return cachedItem.uuid == item.uuid;
        });
        
        if (it != diskCached.end()) {
            diskCached.erase(it);
        }
        
        // Add to history
        HistoryItem historyItem;
        historyItem.uuid = item.uuid;
        historyItem.version = item.version;
        historyItem.downloadDate = item.downloadDate;
        historyItem.deletedDate = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        addHistoryItem(historyItem);
        
        // And delete the metadata from disk too
        deleteMetadata(item.uuid);
    }
    
    // Save changes
    if (!itemsToRemove.empty()) {
        saveDiskCachedToJson();
    }
}

void CacheManager::removeUnknownVideos() {
    fs::path folderPath = PathManager::getInstance().mp4Path();
    
    if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
        throw std::runtime_error("Invalid folder path");
    }
    
    std::vector<fs::path> filesToRemove;
    
    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (fs::is_regular_file(entry) && entry.path().extension() == ".mp4") {
            std::string filename = entry.path().stem().string();
            
            // Check if the filename (without extension) exists in diskCached
            auto it = std::find_if(diskCached.begin(), diskCached.end(),
                                   [&filename](const DiskCachedItem& item) {
                return item.uuid == filename;
            });
            
            if (it == diskCached.end()) {
                filesToRemove.push_back(entry.path());
            }
        }
    }
    
    // Remove the unknown files
    for (const auto& file : filesToRemove) {
        try {
            fs::remove(file);
            g_Log->Info("Removed unknown file: %s", file.string().c_str());
        } catch (const fs::filesystem_error& e) {
            g_Log->Error("Error removing file: %s", file.string().c_str());
        }
    }
}

bool CacheManager::deleteDream(const std::string& uuid) {
    // Check if the dream exists in diskCached
    if (!hasDiskCachedItem(uuid)) {
        g_Log->Warning("Attempt to delete non-existent dream: %s", uuid.c_str());
        return false;
    }

    // Get the dream's file path
    std::string filePath = getDreamPath(uuid);
    if (filePath.empty()) {
        g_Log->Error("Unable to get file path for dream: %s", uuid.c_str());
        return false;
    }

    // Remove the file
    try {
        boost::filesystem::remove(filePath);
        g_Log->Info("Deleted dream file: %s", filePath.c_str());
    } catch (const boost::filesystem::filesystem_error& e) {
        g_Log->Error("Failed to delete dream file: %s. Error: %s", filePath.c_str(), e.what());
        return false;
    }

    // Remove from diskCached
    DiskCachedItem item;
    for (const auto& cachedItem : diskCached) {
        if (cachedItem.uuid == uuid) {
            item = cachedItem;
            break;
        }
    }
    removeDiskCachedItem(uuid);

    // Add to history
    HistoryItem historyItem;
    historyItem.uuid = item.uuid;
    historyItem.version = item.version;
    historyItem.downloadDate = item.downloadDate;
    historyItem.deletedDate = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    addHistoryItem(historyItem);

    // Delete the metadata file
    deleteMetadata(uuid);

    // Save changes to disk
    saveDiskCachedToJson();
    saveHistoryToJson();

    g_Log->Info("Successfully deleted dream: %s", uuid.c_str());
    return true;
}

const std::unordered_map<std::string, Dream>& CacheManager::getDreams() const {
    return dreams;
}

// MARK: - Quota

void CacheManager::decreaseRemainingQuota(long long amount) {
    if (amount > remainingQuota) {
        remainingQuota = 0;
    } else {
        remainingQuota -= amount;
    }
}

// MARK: - Metadata

void CacheManager::loadCachedMetadata() {
    // Also load our internal Caches here !
    loadDiskCachedFromJson();
    loadHistoryFromJson();
    // Make sure our cache is clean, we start by removing metadata no longer linked to files
    cleanupDiskCache();
    // Also remove unknown videos
    removeUnknownVideos();
    
    fs::path dir = PathManager::getInstance().jsonDreamPath();
            
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        g_Log->Error("loadCachedMetadata, cannot find directory");
        return;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto& path = entry.path();
        if (fs::is_regular_file(path) && path.extension() == ".json") {
            g_Log->Debug("Processing JSON file: %s", path.filename().c_str());

            loadJsonFile(path.string());
        }
    }
    
    g_Log->Info("Local metadata cache initialized with %i dreams", dreams.size());
}


bool CacheManager::areMetadataCached(std::string uuid) {
    fs::path filename = PathManager::getInstance().jsonDreamPath() / (uuid + ".json");
    return fs::exists(filename);
}

bool CacheManager::needsMetadata(std::string uuid, long long timeStamp) {
    // Is it cached ?
    if (!hasDream(uuid)) {
        return true;
    } else {
        // Is the playlist timestamp newer ?
        return dreams[uuid].timestamp < timeStamp;
    }
}

void CacheManager::reloadMetadata(std::string uuid) {
    fs::path filename = PathManager::getInstance().jsonDreamPath() / (uuid + ".json");
    loadJsonFile(filename.string());
    
    return;
}

bool CacheManager::deleteMetadata(const std::string& uuid) {
    fs::path metadataPath = PathManager::getInstance().jsonDreamPath();
        
    if (!fs::exists(metadataPath) || !fs::is_directory(metadataPath)) {
        g_Log->Error("Invalid metadata path: %s", metadataPath.c_str());
        return false;
    }

    fs::path filePath = metadataPath / (uuid + ".json");
    
    if (!fs::exists(filePath)) {
        g_Log->Error("Metadata file does not exist: %s", filePath.c_str());
        return false;
    }

    try {
        fs::remove(filePath);
        g_Log->Info("Metadata file removed: %s", filePath.string().c_str());
        return true;
    } catch (const fs::filesystem_error& e) {
        g_Log->Error("Error removing metadata file: %s %s", filePath.string().c_str(), e.what());
        return false;
    }
}

// MARK: Immediate playback
void CacheManager::cacheAndPlayImmediately(const std::string& uuid) {
    // First we may need the metadata
    if (!areMetadataCached(uuid)) {
        EDreamClient::FetchDreamMetadata(uuid);
        reloadMetadata(uuid);
    }
    
    // Then we need to downlaod the file asynchronously
    // We provide a callback so we can playthedream as soon as it's there
    auto future = g_ContentDownloader().m_gDownloader.DownloadImmediately(uuid, [](bool success, const std::string& uuid) {
        if (success) {
            g_Log->Info("Immediate download completed successfully for UUID: ", uuid.c_str());
            g_Player().PlayDreamNow(uuid, -1);
        } else {
            g_Log->Error("Immediate download failed for UUID: ", uuid.c_str());
        }
    });
}




// MARK: - Disk space management
std::uintmax_t CacheManager::getUsedSpace(const fs::path& path) const {
    try {
        if (!fs::exists(path)) {
            g_Log->Error("Path does not exist: %s", path.string().c_str());
            return 0;
        }
        
        if (!fs::is_directory(path)) {
            g_Log->Error("Path is not a directory: %s", path.string().c_str());
            return 0;
        }

        std::uintmax_t size = 0;
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (fs::is_regular_file(entry)) {
                size += fs::file_size(entry);
            }
        }
        return size;
    } catch (const fs::filesystem_error& e) {
        g_Log->Error("Filesystem error in getUsedSpace: %s", e.what());
        return 0;
    } catch (const std::exception& e) {
        g_Log->Error("Unexpected error in getUsedSpace: %s", e.what());
        return 0;
    }
}

std::uintmax_t CacheManager::getFreeSpace(const fs::path& path) const {
    try {
        if (!fs::exists(path)) {
            throw std::runtime_error("Path does not exist");
        }
        
        fs::space_info space = fs::space(path);
        return space.available;
    } catch (const fs::filesystem_error& e) {
        g_Log->Error("Filesystem error in getFreeSpace: %s", e.what());
        return 0;
    } catch (const std::exception& e) {
        g_Log->Error("Unexpected error in getFreeSpace: %s", e.what());
        return 0;
    }
}

// Calculate remaining cache space
std::uintmax_t CacheManager::getRemainingCacheSpace() {
    auto freeSpace = getFreeSpace(PathManager::getInstance().mp4Path());
    
    if (g_Settings()->Get("settings.content.unlimited_cache", true) == true) {
        // Cache can be set to unlimited, which is default
        return freeSpace;
    } else {
        // if not unlimited, default cache is 5 GB
        std::uintmax_t cacheSize = (std::uintmax_t)g_Settings()->Get("settings.content.cache_size", 5) * 1024 * 1024 * 1024;
        std::uintmax_t usedSpace = getUsedSpace(PathManager::getInstance().mp4Path());

        return (cacheSize > usedSpace) ? (cacheSize - usedSpace) : 0;
    }
}

double CacheManager::getCacheSize() const {
    const fs::path mp4Path = PathManager::getInstance().mp4Path();
    auto totalSizeBytes = getUsedSpace(mp4Path);

    // Convert bytes to GB
    constexpr double bytesPerGB = 1024.0 * 1024.0 * 1024.0;
    return static_cast<double>(totalSizeBytes) / bytesPerGB;
}

// MARK: - DiskCacheItem/HistoryItem

void CacheManager::addDiskCachedItem(const DiskCachedItem& item) {
    auto it = std::find_if(diskCached.begin(), diskCached.end(),
        [&item](const DiskCachedItem& existingItem) {
            return existingItem.uuid == item.uuid;
        });

    if (it != diskCached.end()) {
        // UUID already exists, replace the existing item
        *it = item;
    } else {
        // UUID doesn't exist, add new item
        diskCached.push_back(item);
    }

    saveDiskCachedToJson();
}

void CacheManager::removeDiskCachedItem(const std::string& uuid) {
    auto it = std::find_if(diskCached.begin(), diskCached.end(),
        [&uuid](const DiskCachedItem& item) {
            return item.uuid == uuid;
        });

    if (it != diskCached.end()) {
        diskCached.erase(it);
        saveDiskCachedToJson();
    } else {
        throw std::runtime_error("Item with specified UUID not found in disk cache");
    }
}

bool CacheManager::hasDiskCachedItem(const std::string& uuid) const {
    return std::any_of(diskCached.begin(), diskCached.end(),
        [&uuid](const DiskCachedItem& item) {
            return item.uuid == uuid;
        });
}

void CacheManager::addHistoryItem(const HistoryItem& item) {
    history.push_back(item);
    saveHistoryToJson();
}

void CacheManager::saveDiskCachedToJson() const {
    boost::property_tree::ptree root;
    boost::property_tree::ptree items;

    for (const auto& item : diskCached) {
        items.push_back(std::make_pair("", serializeDiskCachedItem(item)));
    }

    root.add_child("diskCached", items);
    
    fs::path fileName = PathManager::getInstance().rootPath() / "diskcached.json";
    
    boost::property_tree::write_json(fileName.string(), root);
}

void CacheManager::saveHistoryToJson() const {
    boost::property_tree::ptree root;
    boost::property_tree::ptree items;

    for (const auto& item : history) {
        items.push_back(std::make_pair("", serializeHistoryItem(item)));
    }

    root.add_child("history", items);
    
    fs::path fileName = PathManager::getInstance().rootPath() / "history.json";

    boost::property_tree::write_json(fileName.string(), root);
}

void CacheManager::loadDiskCachedFromJson() {
    try {
        boost::property_tree::ptree root;
        fs::path filePath = PathManager::getInstance().rootPath() / "diskcached.json";

        if (!fs::exists(filePath)) {
            g_Log->Info("Disk cached file does not exist: %s", filePath.string().c_str());
            diskCached.clear();
            return;
        }

        boost::property_tree::read_json(filePath.string(), root);

        diskCached.clear();
        for (const auto& item : root.get_child("diskCached")) {
            diskCached.push_back(deserializeDiskCachedItem(item.second));
        }
    } catch (const boost::property_tree::json_parser_error& e) {
        g_Log->Error("JSON parsing error in loadDiskCachedFromJson: %s", e.what());
        diskCached.clear();
    } catch (const std::exception& e) {
        g_Log->Error("Unexpected error in loadDiskCachedFromJson: %s", e.what());
        diskCached.clear();
    }
}

void CacheManager::loadHistoryFromJson() {
    try {
        boost::property_tree::ptree root;
        fs::path filePath = PathManager::getInstance().rootPath() / "history.json";

        if (!fs::exists(filePath)) {
            g_Log->Info("History file does not exist: %s", filePath.string().c_str());
            history.clear();
            return;
        }

        boost::property_tree::read_json(filePath.string(), root);

        history.clear();
        for (const auto& item : root.get_child("history")) {
            history.push_back(deserializeHistoryItem(item.second));
        }
    } catch (const boost::property_tree::json_parser_error& e) {
        g_Log->Error("JSON parsing error in loadHistoryFromJson: %s", e.what());
        history.clear();
    } catch (const std::exception& e) {
        g_Log->Error("Unexpected error in loadHistoryFromJson: %s", e.what());
        history.clear();
    }
}

boost::property_tree::ptree CacheManager::serializeDiskCachedItem(const DiskCachedItem& item) const {
    boost::property_tree::ptree pt;
    pt.put("uuid", item.uuid);
    pt.put("version", item.version);
    pt.put("downloadDate", item.downloadDate);
    return pt;
}

boost::property_tree::ptree CacheManager::serializeHistoryItem(const HistoryItem& item) const {
    boost::property_tree::ptree pt;
    pt.put("uuid", item.uuid);
    pt.put("version", item.version);
    pt.put("downloadDate", item.downloadDate);
    pt.put("deletedDate", item.deletedDate);
    return pt;
}

CacheManager::DiskCachedItem CacheManager::deserializeDiskCachedItem(const boost::property_tree::ptree& pt) const {
    DiskCachedItem item;
    item.uuid = pt.get<std::string>("uuid");
    item.version = pt.get<long long>("version");
    item.downloadDate = pt.get<long long>("downloadDate");
    return item;
}

CacheManager::HistoryItem CacheManager::deserializeHistoryItem(const boost::property_tree::ptree& pt) const {
    HistoryItem item;
    item.uuid = pt.get<std::string>("uuid");
    item.version = pt.get<long long>("version");
    item.downloadDate = pt.get<long long>("downloadDate");
    item.deletedDate = pt.get<long long>("deletedDate");
    return item;
}

// MARK: Dislike/evicted UUIDs
void CacheManager::addEvictedUUID(const std::string& uuid) {
    if (std::find(m_evictedUUIDs.begin(), m_evictedUUIDs.end(), uuid) == m_evictedUUIDs.end()) {
        m_evictedUUIDs.push_back(uuid);
        saveEvictedUUIDsToJson();
    }
}

bool CacheManager::isUUIDEvicted(const std::string& uuid) const {
    return std::find(m_evictedUUIDs.begin(), m_evictedUUIDs.end(), uuid) != m_evictedUUIDs.end();
}

const std::vector<std::string>& CacheManager::getEvictedUUIDs() const {
    return m_evictedUUIDs;
}

void CacheManager::clearEvictedUUIDs() {
    m_evictedUUIDs.clear();
    saveEvictedUUIDsToJson();
}

void CacheManager::saveEvictedUUIDsToJson() const {
    boost::property_tree::ptree root;
    root.add_child("evictedUUIDs", serializeEvictedUUIDs());
    
    fs::path fileName = PathManager::getInstance().rootPath() / "evicted_uuids.json";
    
    boost::property_tree::write_json(fileName.string(), root);
}

void CacheManager::loadEvictedUUIDsFromJson() {
    try {
        boost::property_tree::ptree root;
        fs::path filePath = PathManager::getInstance().rootPath() / "evicted_uuids.json";

        if (!fs::exists(filePath)) {
            g_Log->Info("Evicted UUIDs file does not exist: %s", filePath.string().c_str());
            m_evictedUUIDs.clear();
            return;
        }

        boost::property_tree::read_json(filePath.string(), root);

        deserializeEvictedUUIDs(root.get_child("evictedUUIDs"));
    } catch (const boost::property_tree::json_parser_error& e) {
        g_Log->Error("JSON parsing error in loadEvictedUUIDsFromJson: %s", e.what());
        m_evictedUUIDs.clear();
    } catch (const std::exception& e) {
        g_Log->Error("Unexpected error in loadEvictedUUIDsFromJson: %s", e.what());
        m_evictedUUIDs.clear();
    }
}

boost::property_tree::ptree CacheManager::serializeEvictedUUIDs() const {
    boost::property_tree::ptree pt;
    for (const auto& uuid : m_evictedUUIDs) {
        boost::property_tree::ptree uuid_node;
        uuid_node.put("", uuid);
        pt.push_back(std::make_pair("", uuid_node));
    }
    return pt;
}

void CacheManager::deserializeEvictedUUIDs(const boost::property_tree::ptree& pt) {
    m_evictedUUIDs.clear();
    for (const auto& item : pt) {
        m_evictedUUIDs.push_back(item.second.get_value<std::string>());
    }
}

} // Namespace
