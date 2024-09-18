//
//  ServerConfig.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 30/07/2024.
//

#include "ServerConfig.h"
#include "Settings.h" // Include for g_Settings()

namespace ServerConfig {

ServerConfigManager::ServerConfigManager() {
    m_dreamServer = "https://" + g_Settings()->Get("settings.content.server", std::string(DEFAULT_DREAM_SERVER));
    m_websocketServer = "wss://" + g_Settings()->Get("settings.content.server", std::string(DEFAULT_DREAM_SERVER));
    initializeEndpoints();
}

void ServerConfigManager::initializeEndpoints() {
    std::string apiVersionV1(API_VERSION_V1);
    std::string apiVersionV2(API_VERSION_V2);
    
    m_endpoints = {
        {Endpoint::HELLO, m_dreamServer + apiVersionV1 + "/client/hello"},
        {Endpoint::GETPLAYLIST, m_dreamServer + apiVersionV1 + "/client/playlist"},
        {Endpoint::GETDEFAULTPLAYLIST, m_dreamServer + apiVersionV1 + "/client/playlist/default"},
        {Endpoint::GETDREAM, m_dreamServer + apiVersionV1 + "/client/dream"},
        // New V2 endpoints
        {Endpoint::LOGIN_MAGIC, m_dreamServer + apiVersionV2 + "/auth/magic"},
        {Endpoint::LOGIN_AUTHENTICATE, m_dreamServer + apiVersionV2 + "/auth/authenticate"},
        {Endpoint::LOGIN_REFRESH, m_dreamServer + apiVersionV2 + "/auth/refresh"}

    };
}

std::string ServerConfigManager::getDreamServer() const {
    return m_dreamServer;
}

std::string ServerConfigManager::getWebsocketServer() const {
    return m_websocketServer;
}

std::string ServerConfigManager::getEndpoint(Endpoint endpoint) const {
    auto it = m_endpoints.find(endpoint);
    if (it != m_endpoints.end()) {
        return it->second;
    }
    return "";
}

} // namespace ServerConfig
