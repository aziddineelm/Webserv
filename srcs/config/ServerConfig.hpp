#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

struct LocationContext {
    std::string path;
    std::vector<std::string> allowed_methods;
    std::string root;
    std::string alias;
    bool autoindex;
    std::string index;
    std::pair<int, std::string> redirect;
    std::string upload_store;
    std::vector<std::string> cgi_extensions;
    std::string cgi_path;
    std::map<std::string, std::string> cgi_map;

    LocationContext();
};

class ServerConfig {
public:
    uint16_t listen_port;
    std::string host;
    std::vector<std::string> server_names;
    std::string root;
    std::string index;
    size_t client_max_body_size;
    std::map<int, std::string> error_pages;
    std::map<std::string, LocationContext> locations;

    ServerConfig();
    void printConfig() const;
};

#endif