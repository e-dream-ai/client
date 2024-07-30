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
    std::string apiVersion(API_VERSION);
    m_endpoints = {
        {Endpoint::DREAM, m_dreamServer + apiVersion + "/dream"},
        {Endpoint::LOGIN, m_dreamServer + apiVersion + "/auth/login"},
        {Endpoint::REFRESH, m_dreamServer + apiVersion + "/auth/refresh"},
        {Endpoint::USER, m_dreamServer + apiVersion + "/auth/user"},
        {Endpoint::PLAYLIST, m_dreamServer + apiVersion + "/playlist"},
        {Endpoint::CURRENTPLAYLIST, m_dreamServer + apiVersion + "/user/current/playlist"},
        {Endpoint::HELLO, m_dreamServer + apiVersion + "/client/hello"},
        {Endpoint::GETPLAYLIST, m_dreamServer + apiVersion + "/client/playlist"},
        {Endpoint::GETDEFAULTPLAYLIST, m_dreamServer + apiVersion + "/client/playlist/default"},
        {Endpoint::GETDREAM, m_dreamServer + apiVersion + "/client/dream"}
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
