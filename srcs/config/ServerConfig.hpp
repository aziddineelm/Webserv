#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

// Describes configuration for a single `location` block.
// Contains path, allowed methods, root/alias, autoindex flag,
// index file name, optional redirect, upload storage, and CGI settings.
struct LocationConfig {
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
    // Redirect URL (empty = no redirect).
    std::string redirect_url;
    // Redirect HTTP status code (0 = no redirect).
    int redirect_code;
    // Directory where uploaded files should be stored.
    std::string upload_store;
    // File extensions handled by CGI for this location.
    std::vector<std::string> cgi_extensions;
    // Path to the CGI executable or handler.
    std::string cgi_path;
    // Mapping from extension to CGI handler path.
    std::map<std::string, std::string> cgi_map;
    // Maximum allowed size for client request bodies (inherited from server).
    size_t client_max_body_size;
    // Mapping of HTTP status codes to custom error page paths (inherited from server).
    std::map<int, std::string> error_pages;

    // Default constructor initializes sensible defaults.
    LocationConfig();
};

// Represents configuration for a single `server` block.
// Includes listening port, host, server names, root, index, upload
// size limits, error pages, and a collection of `LocationContext`s.
class ServerConfig {
public:
    // TCP ports to listen on (e.g., 80, 8080). Supports multiple listen directives.
    std::vector<uint16_t> listen_ports;
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
    std::map<std::string, LocationConfig> locations;

    // Default constructor initializes sensible defaults.
    ServerConfig();

    // Utility to print the server configuration (for debugging).
    void printConfig() const;

    // Find the best matching location for a given URI (longest prefix match).
    // Returns NULL if no location matches.
    const LocationConfig* matchLocation(const std::string& uri) const;

    // Get all locations as a flat vector (useful for Person B's Router).
    std::vector<LocationConfig> getLocationList() const;
};

#endif