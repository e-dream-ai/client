//
//  CacheManager.h
//  e-dream
//
//  Created by Guillaume Louel on 09/07/2024.
//

#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <memory>

class CacheManager {
public:
    // Delete copy constructor and assignment operator
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    // Get the singleton instance
    static CacheManager& getInstance();

    
    bool isCached(std::string uuid);
    

private:
    // Private constructor
    CacheManager() = default;

    // The singleton instance
    static std::unique_ptr<CacheManager> instance;
};

#endif // CACHE_MANAGER_H
