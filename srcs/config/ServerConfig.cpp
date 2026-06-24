#include "ServerConfig.hpp"
#include <iostream>

LocationContext::LocationContext() : autoindex(false) {}

ServerConfig::ServerConfig() : host("127.0.0.1"), client_max_body_size(1048576) {
    listen_ports.push_back(8080);
}

void ServerConfig::printConfig() const {
    std::cout << "Server Config:" << std::endl;
    std::cout << "  Listen Ports:";
    for (size_t i = 0; i < listen_ports.size(); ++i) {
        std::cout << " " << listen_ports[i];
    }
    std::cout << std::endl;
    std::cout << "  Host: " << host << std::endl;
    std::cout << "  Root: " << root << std::endl;
    std::cout << "  Max Body Size: " << client_max_body_size << std::endl;
}

const LocationContext* ServerConfig::matchLocation(const std::string& uri) const {
    const LocationContext* best = NULL;
    size_t bestLen = 0;

    for (std::map<std::string, LocationContext>::const_iterator it = locations.begin();
         it != locations.end(); ++it) {
        const std::string& prefix = it->second.path;

        // URI must start with the location path
        if (uri.compare(0, prefix.size(), prefix) == 0) {
            // For non-root locations, ensure boundary match
            if (prefix != "/" && prefix.size() < uri.size()
                && uri[prefix.size()] != '/')
                continue;

            // Longest prefix wins
            if (prefix.size() > bestLen) {
                bestLen = prefix.size();
                best = &(it->second);
            }
        }
    }
    return best;
}

std::vector<LocationContext> ServerConfig::getLocationList() const {
    std::vector<LocationContext> list;
    for (std::map<std::string, LocationContext>::const_iterator it = locations.begin();
         it != locations.end(); ++it) {
        list.push_back(it->second);
    }
    return list;
}
