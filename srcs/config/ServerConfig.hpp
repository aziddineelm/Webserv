#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

// Describes configuration for a single `location` block.
// Contains path, allowed methods, root/alias, autoindex flag,
// index file name, optional redirect, upload storage, and CGI settings.
struct LocationContext {
    // Location path (e.g., "/images").
    std::string path;
    // Allowed HTTP methods for this location (GET, POST, DELETE, ...).
    std::vector<std::string> allowed_methods;
    // Filesystem root for this location.
    std::string root;
    // Alias path if configured instead of root.
    std::string alias;
    // Whether directory listings are enabled.
    bool autoindex;
    // Default index file name for this location.
    std::string index;
    // Optional redirect as (status_code, url).
    std::pair<int, std::string> redirect;
    // Directory where uploaded files should be stored.
    std::string upload_store;
    // File extensions handled by CGI for this location.
    std::vector<std::string> cgi_extensions;
    // Path to the CGI executable or handler.
    std::string cgi_path;
    // Mapping from extension to CGI handler path.
    std::map<std::string, std::string> cgi_map;

    // Default constructor initializes sensible defaults.
    LocationContext();
};

// Represents configuration for a single `server` block.
// Includes listening port, host, server names, root, index, upload
// size limits, error pages, and a collection of `LocationContext`s.
class ServerConfig {
public:
    // TCP port to listen on (e.g., 80, 8080).
    uint16_t listen_port;
    // Host/IP to bind to (commonly "0.0.0.0" or "127.0.0.1").
    std::string host;
    // Server names (virtual host names) for this server.
    std::vector<std::string> server_names;
    // Document root for the server.
    std::string root;
    // Default index file name.
    std::string index;
    // Maximum allowed size for client request bodies (bytes).
    size_t client_max_body_size;
    // Mapping of HTTP status codes to custom error page paths.
    std::map<int, std::string> error_pages;
    // Named locations configured under this server.
    std::map<std::string, LocationContext> locations;

    // Default constructor initializes sensible defaults.
    ServerConfig();

    // Utility to print the server configuration (for debugging).
    void printConfig() const;
};

#endif