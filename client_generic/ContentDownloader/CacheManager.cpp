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

std::unique_ptr<CacheManager> CacheManager::instance;

CacheManager& CacheManager::getInstance() {
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new CacheManager()); });
    return *instance;
}

bool CacheManager::isCached(std::string uuid) {
    return false;
}
