//
//  CacheManager.cpp
//  e-dream
//
//  Created by Guillaume Louel on 09/07/2024.
//
//  This class manages the local cache and makes sure we have the correct associated metadata

#include "CacheManager.h"
#include <fstream>
#include <iostream>
#include <mutex>
#include <boost/json.hpp>
#include <boost/filesystem.hpp>

#include "StringFormat.h"
#include "Shepherd.h"
#include "Settings.h"
#include "Log.h"

namespace Cache {

using boost::filesystem::exists;
namespace fs = boost::filesystem;

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
    
    std::string dest = ContentDownloader::Shepherd::mp4Path();
    boost::filesystem::path folderPath(dest);
    boost::filesystem::path filePath = folderPath / (uuid + ".mp4");

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
            dream.size = safe_get_string(dream_obj, "size");
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

    std::string dest = ContentDownloader::Shepherd::mp4Path();
    boost::filesystem::path folderPath(dest);
    
    // Look for files that may have been deleted
    for (const auto& item : diskCached) {
        boost::filesystem::path filePath = folderPath / (item.uuid + ".mp4");
        
        if (!boost::filesystem::exists(filePath)) {
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
    std::string dest = ContentDownloader::Shepherd::mp4Path();
    boost::filesystem::path folderPath(dest);
    
    if (!boost::filesystem::exists(folderPath) || !boost::filesystem::is_directory(folderPath)) {
        throw std::runtime_error("Invalid folder path");
    }

    std::vector<boost::filesystem::path> filesToRemove;

    for (const auto& entry : boost::filesystem::directory_iterator(folderPath)) {
        if (boost::filesystem::is_regular_file(entry) && entry.path().extension() == ".mp4") {
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
            boost::filesystem::remove(file);
            g_Log->Info("Removed unknown file: %s", file.string().c_str());
        } catch (const boost::filesystem::filesystem_error& e) {
            g_Log->Error("Error removing file: %s", file.string().c_str());
        }
    }
}

const std::unordered_map<std::string, Dream>& CacheManager::getDreams() const {
    return dreams;
}

void CacheManager::loadCachedMetadata() {
    // Also load our internal Caches here !
    loadDiskCachedFromJson();
    loadHistoryFromJson();
    // Make sure our cache is clean, we start by removing metadata no longer linked to files
    cleanupDiskCache();
    // Also remove unknown videos
    removeUnknownVideos();
    
    std::string dest = ContentDownloader::Shepherd::jsonDreamPath();
    boost::filesystem::path dir(dest);
        
    if (!boost::filesystem::exists(dir) || !boost::filesystem::is_directory(dir)) {
        g_Log->Error("loadCachedMetadata, cannot find directory");
        return;
    }

    for (const auto& entry : boost::filesystem::directory_iterator(dir)) {
        const auto& path = entry.path();
        if (boost::filesystem::is_regular_file(path) && path.extension() == ".json") {
            g_Log->Debug("Processing JSON file: %s", path.filename().c_str());

            loadJsonFile(path.c_str());
        }
    }
    
    g_Log->Info("Local metadata cache initialized with %i dreams", dreams.size());
}


bool CacheManager::areMetadataCached(std::string uuid) {
    std::string filename{string_format("%s%s.json", ContentDownloader::Shepherd::jsonDreamPath(), uuid.c_str())};

    return exists(filename);
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

    std::string filename{string_format("%s%s.json", ContentDownloader::Shepherd::jsonDreamPath(), uuid.c_str())};
    
    loadJsonFile(filename);
    
    return;
}

bool CacheManager::deleteMetadata(const std::string& uuid) {
    std::string dest = ContentDownloader::Shepherd::jsonDreamPath();
    boost::filesystem::path metadataPath(dest);
    
    if (!boost::filesystem::exists(metadataPath) || !boost::filesystem::is_directory(metadataPath)) {
        g_Log->Error("Invalid metadata path: %s", metadataPath.c_str());
        return false;
    }

    boost::filesystem::path filePath = metadataPath / (uuid + ".json");
    
    if (!boost::filesystem::exists(filePath)) {
        g_Log->Error("Metadata file does not exist: %s", filePath.c_str());
        return false;
    }

    try {
        boost::filesystem::remove(filePath);
        g_Log->Info("Metadata file removed: %s", filePath.c_str());
        return true;
    } catch (const boost::filesystem::filesystem_error& e) {
        g_Log->Error("Metadata file does not exist: %s %s", filePath.c_str(), e.what());
        return false;
    }
}


// MARK: - Disk space management
std::uintmax_t CacheManager::getUsedSpace(const char* path) {
    try {
        fs::path p(path);
        if (!fs::exists(p)) {
            throw std::runtime_error("Path does not exist");
        }
        
        if (fs::is_directory(p)) {
            return fs::file_size(p);
        } else {
            std::uintmax_t size = 0;
            fs::recursive_directory_iterator end;
            for (fs::recursive_directory_iterator it(p); it != end; ++it) {
                if (!fs::is_directory(*it)) {
                    size += fs::file_size(*it);
                }
            }
            return size;
        }
    } catch (const fs::filesystem_error& e) {
        throw std::runtime_error(std::string("Filesystem error: ") + e.what());
    }
}

// Function to get the remaining free space on the disk
std::uintmax_t CacheManager::getFreeSpace(const char* path) {
    try {
        fs::path p(path);
        if (!fs::exists(p)) {
            throw std::runtime_error("Path does not exist");
        }
        
        fs::space_info space = fs::space(p);
        return space.available;
    } catch (const fs::filesystem_error& e) {
        throw std::runtime_error(std::string("Filesystem error: ") + e.what());
    }
}

// Calculate remaining cache space
std::uintmax_t CacheManager::getRemainingCacheSpace() {
    auto freeSpace = getFreeSpace(ContentDownloader::Shepherd::mp4Path());
    
    if (g_Settings()->Get("settings.content.unlimited_cache", true) == true) {
        // Cache can be set to unlimited, which is default
        return freeSpace;
    } else {
        // if not unlimited, default cache is 2 GB
        auto cacheSize = 1024 * 1024 * g_Settings()->Get("settings.content.cache_size", 2000);
        auto usedSpace = getUsedSpace(ContentDownloader::Shepherd::mp4Path());

        return (cacheSize - usedSpace);
    }
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
    
    std::string fileName{
        string_format("%s%s.json", ContentDownloader::Shepherd::rootPath(), "diskcached")};
    
    boost::property_tree::write_json(fileName, root);
}

void CacheManager::saveHistoryToJson() const {
    boost::property_tree::ptree root;
    boost::property_tree::ptree items;

    for (const auto& item : history) {
        items.push_back(std::make_pair("", serializeHistoryItem(item)));
    }

    root.add_child("history", items);
    
    std::string fileName{
        string_format("%s%s.json", ContentDownloader::Shepherd::rootPath(), "history")};

    boost::property_tree::write_json(fileName, root);
}

void CacheManager::loadDiskCachedFromJson() {
    try {
        boost::property_tree::ptree root;
        std::string fileName{
            string_format("%s%s.json", ContentDownloader::Shepherd::rootPath(), "diskcached")};

        boost::property_tree::read_json(fileName, root);

        diskCached.clear();
        for (const auto& item : root.get_child("diskCached")) {
            diskCached.push_back(deserializeDiskCachedItem(item.second));
        }
    } catch (const std::exception& e) {
        // If file doesn't exist or is invalid, start with an empty vector
        diskCached.clear();
    }
}

void CacheManager::loadHistoryFromJson() {
    try {
        boost::property_tree::ptree root;

        std::string fileName{
            string_format("%s%s.json", ContentDownloader::Shepherd::rootPath(), "history")};

        boost::property_tree::read_json(fileName, root);

        history.clear();
        for (const auto& item : root.get_child("history")) {
            history.push_back(deserializeHistoryItem(item.second));
        }
    } catch (const std::exception& e) {
        // If file doesn't exist or is invalid, start with an empty vector
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


} // Namespace
