//
//  NetworkConfig.h
//  e-dream
//
//  Created by Guillaume Louel on 01/11/2024.
//

#ifndef NetworkConfig_h
#define NetworkConfig_h

#include <string>
#include "PlatformUtils.h"

namespace Network {

// Network request headers configuration
struct NetworkHeaders {
    static std::string getClientType() {
        return "Edream-Client-Type: " + PlatformUtils::GetPlatformName();
    }
    
    static std::string getClientVersion() {
        return "Edream-Client-Version: " + PlatformUtils::GetAppVersion();
    }
    
    // Helper method to add all standard headers to a request
    template<typename T>
    static void addStandardHeaders(T& downloader) {
        downloader->AppendHeader(getClientType());
        downloader->AppendHeader(getClientVersion());
    }
};

} // namespace Network

#endif /* NetworkConfig_h */
