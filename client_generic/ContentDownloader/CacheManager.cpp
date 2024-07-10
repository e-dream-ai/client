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

#include "Log.h"

std::unique_ptr<CacheManager> CacheManager::instance;

CacheManager& CacheManager::getInstance() {
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new CacheManager()); });
    return *instance;
}

void CacheManager::loadJsonFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        g_Log->Error("Failed to open file: %s", filename.c_str());
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    boost::json::error_code ec;
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

            dreams[dream.uuid] = dream;
        }
    }
}

const std::unordered_map<std::string, Dream>& CacheManager::getDreams() const {
    return dreams;
}




bool CacheManager::isCached(std::string uuid) {


    return false;
}

bool CacheManager::needsMetadata(std::string uuid, int timeStamp) {
    
    
    return true;
}

bool CacheManager::fetchMetadata(std::string uuid) {

    return true;
}
