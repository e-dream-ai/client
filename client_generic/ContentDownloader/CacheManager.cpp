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
            Dream dream;
            dream.uuid = dream_json.as_object().at("uuid").as_string().c_str();
            dream.name = dream_json.as_object().at("name").as_string().c_str();
            dream.artist = dream_json.as_object().at("artist").as_string().c_str();
            dream.size = dream_json.as_object().at("size").as_string().c_str();
            dream.status = dream_json.as_object().at("status").as_string().c_str();
            dream.fps = dream_json.as_object().at("fps").as_string().c_str();
            dream.frames = dream_json.as_object().at("frames").as_int64();
            dream.thumbnail = dream_json.as_object().at("thumbnail").as_string().c_str();
            dream.upvotes = dream_json.as_object().at("upvotes").as_int64();
            dream.downvotes = dream_json.as_object().at("downvotes").as_int64();
            dream.nsfw = dream_json.as_object().at("nsfw").as_bool();
            dream.frontendUrl = dream_json.as_object().at("frontendUrl").as_string().c_str();
            dream.video_timestamp = dream_json.as_object().at("video_timestamp").as_int64();
            dream.timestamp = dream_json.as_object().at("timestamp").as_int64();
            
            if (dream_json.as_object().at("activityLevel").is_number()) {
                dream.activityLevel = dream_json.as_object().at("activityLevel").to_number<float>();
            }
            dreams[dream.uuid] = dream;

            /*
            // TEST CODE
            DiskCachedItem newDiskItem;
            newDiskItem.uuid = dream.uuid;
            newDiskItem.version = dream.video_timestamp;
            newDiskItem.downloadDate = dream.video_timestamp;

            addDiskCachedItem(newDiskItem);
            
            HistoryItem newHistoryItem;
            newHistoryItem.uuid = dream.uuid;
            newHistoryItem.version = dream.video_timestamp;
            newHistoryItem.downloadDate = dream.video_timestamp;
            newHistoryItem.deletedDate = dream.video_timestamp;
            addHistoryItem(newHistoryItem);
            // /TEST CODE
             */
            
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
    }

    // Save changes
    if (!itemsToRemove.empty()) {
        saveDiskCachedToJson();
    }
}



const std::unordered_map<std::string, Dream>& CacheManager::getDreams() const {
    return dreams;
}

void CacheManager::loadCachedMetadata() {
    // Also load our internal Caches here !
    loadDiskCachedFromJson();
    loadHistoryFromJson();
    
    
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
