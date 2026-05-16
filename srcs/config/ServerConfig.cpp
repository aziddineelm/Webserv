#include "ServerConfig.hpp"
#include <iostream>

LocationContext::LocationContext() : autoindex(false) {}

ServerConfig::ServerConfig() : listen_port(8080), host("127.0.0.1"), client_max_body_size(1048576) {}

void ServerConfig::printConfig() const {
    std::cout << "Server Config:" << std::endl;
    std::cout << "  Listen Port: " << listen_port << std::endl;
    std::cout << "  Host: " << host << std::endl;
    std::cout << "  Root: " << root << std::endl;
    std::cout << "  Max Body Size: " << client_max_body_size << std::endl;
}
