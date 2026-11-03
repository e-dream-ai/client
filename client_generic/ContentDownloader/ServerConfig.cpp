//
//  ServerConfig.cpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 30/07/2024.
//

#include "ServerConfig.h"
#include "Log.h"
#include "Settings.h" // Include for g_Settings()

namespace ServerConfig {

ServerConfigManager::ServerConfigManager() {
    // A bare host means TLS. "http://" has to be asked for explicitly: it exists so the
    // client can run against a local fake server (client#733).
    std::string server = g_Settings()->Get("settings.content.server", std::string(DEFAULT_DREAM_SERVER));
    while (!server.empty() && server.back() == '/')
        server.pop_back();

    constexpr std::string_view kHttp = "http://";
    constexpr std::string_view kHttps = "https://";
    std::string host = server;
    if (server.rfind(kHttp, 0) == 0) {
        m_isPlaintext = true;
        host = server.substr(kHttp.size());
    } else if (server.rfind(kHttps, 0) == 0) {
        host = server.substr(kHttps.size());
    }

    m_dreamServer = (m_isPlaintext ? "http://" : "https://") + host;
    m_websocketServer = (m_isPlaintext ? "ws://" : "wss://") + host;
    initializeEndpoints();

    if (m_isPlaintext) {
        g_Log->Warning("Server connection is UNENCRYPTED (%s). Stored logins are not sent; "
                       "only INFINIDREAM_API_KEY is used.", m_dreamServer.c_str());
    }
}

void ServerConfigManager::initializeEndpoints() {
    std::string apiVersionV1(API_VERSION_V1);
    std::string apiVersionV2(API_VERSION_V2);
    
    m_endpoints = {
        {Endpoint::HELLO, m_dreamServer + apiVersionV1 + "/client/hello"},
        {Endpoint::GETPLAYLIST, m_dreamServer + apiVersionV1 + "/client/playlist"},
        {Endpoint::GETDEFAULTPLAYLIST, m_dreamServer + apiVersionV1 + "/client/playlist/default"},
        {Endpoint::GETDREAM, m_dreamServer + apiVersionV1 + "/client/dream"},
        {Endpoint::GETDISLIKES, m_dreamServer + apiVersionV1 + "/client/user/dislikes"},
        {Endpoint::TELEMETRY, m_dreamServer + apiVersionV1 + "/client/telemetry"},
        {Endpoint::QUOTA, m_dreamServer + apiVersionV1 + "/client/quota"},
        // New V2 endpoints
        {Endpoint::LOGIN_MAGIC, m_dreamServer + apiVersionV2 + "/auth/magic"},
        {Endpoint::LOGIN_AUTHENTICATE, m_dreamServer + apiVersionV2 + "/auth/authenticate"},
        {Endpoint::LOGIN_REFRESH, m_dreamServer + apiVersionV2 + "/auth/refresh"},
        {Endpoint::LOGOUT, m_dreamServer + apiVersionV2 + "/auth/logout"}

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
