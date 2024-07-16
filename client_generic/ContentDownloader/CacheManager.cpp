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
#include "Log.h"

namespace Cache {

using boost::filesystem::exists;

std::unique_ptr<CacheManager> CacheManager::instance;

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
    return dreams.size();
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
        }
    }
}

const std::unordered_map<std::string, Dream>& CacheManager::getDreams() const {
    return dreams;
}

void CacheManager::loadCachedMetadata() {
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

} // Namespace
