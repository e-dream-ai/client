//
//  ServerConfig.hpp
//  ScreenSaver
//
//  Created by Guillaume Louel on 30/07/2024.
//

#ifndef SERVER_CONFIG_MANAGER_H
#define SERVER_CONFIG_MANAGER_H

#include <string>
#include <string_view>
#include <map>

namespace ServerConfig {

constexpr std::string_view DEFAULT_DREAM_SERVER =
#ifdef STAGE
    "e-dream-76c98b08cc5d.herokuapp.com";
#else
    "e-dream-prod-84baba5507ee.herokuapp.com";
#endif

constexpr std::string_view API_VERSION = "/api/v1";

enum class Endpoint {
    DREAM,
    LOGIN,
    REFRESH,
    USER,
    PLAYLIST,
    CURRENTPLAYLIST,
    HELLO,
    GETPLAYLIST,
    GETDEFAULTPLAYLIST,
    GETDREAM
};

class ServerConfigManager {
public:
    static ServerConfigManager& getInstance() {
        static ServerConfigManager instance;
        return instance;
    }

    std::string getDreamServer() const;
    std::string getWebsocketServer() const;
    std::string getEndpoint(Endpoint endpoint) const;

private:
    ServerConfigManager();
    ServerConfigManager(const ServerConfigManager&) = delete;
    ServerConfigManager& operator=(const ServerConfigManager&) = delete;

    void initializeEndpoints();

    std::string m_dreamServer;
    std::string m_websocketServer;
    std::map<Endpoint, std::string> m_endpoints;
};

} // namespace ServerConfig

#endif // SERVER_CONFIG_MANAGER_H
