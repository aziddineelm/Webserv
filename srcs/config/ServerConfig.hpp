#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

struct LocationConfig {
    std::string path;
    std::vector<std::string> allowed_methods;
    std::string root;
    std::string alias;
    bool autoindex;
    std::string index;
    std::string redirect_url;
    int redirect_code;
    std::string upload_store;
    std::vector<std::string> cgi_extensions;
    std::string cgi_path;
    std::map<std::string, std::string> cgi_map;
    size_t client_max_body_size;
    std::map<int, std::string> error_pages;
    int cgi_idle_timeout;
    int cgi_max_timeout;

    LocationConfig();
};

class ServerConfig {
public:
    std::vector<uint16_t> listen_ports;
    std::string host;
    std::vector<std::string> server_names;
    std::string root;
    std::string index;
    size_t client_max_body_size;
    std::map<int, std::string> error_pages;
    std::map<std::string, LocationConfig> locations;

    ServerConfig();

    void printConfig() const;
    const LocationConfig* matchLocation(const std::string& uri) const;

    std::vector<LocationConfig> getLocationList() const;
};

#endif