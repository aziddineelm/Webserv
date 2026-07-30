// All validation TODOs completed (error_page existence, duplicate listen pairs, allowed_methods enforcement)
#include "ConfigParser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>
#include <set>
#include <utility>

// --- Exception Implementation ---
ConfigParser::ConfigException::ConfigException(const std::string& msg) : _msg(msg) {}
ConfigParser::ConfigException::~ConfigException() throw() {}
const char* ConfigParser::ConfigException::what() const throw() { return _msg.c_str(); }

ConfigParser::ConfigParser(const std::string& configFilePath) : _configFilePath(configFilePath) {}

ConfigParser::~ConfigParser() {}

namespace {
    bool isNumber(const std::string& value) {
        if (value.empty()) return false;
        for (size_t i = 0; i < value.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
                return false;
            }
        }
        return true;
    }

    size_t parseSizeWithUnit(const std::string& value) {
        if (value.empty()) return static_cast<size_t>(-1);
        char unit = value[value.size() - 1];
        std::string numberPart = value;
        size_t multiplier = 1;

        if (!std::isdigit(static_cast<unsigned char>(unit))) {
            numberPart = value.substr(0, value.size() - 1);
            if (unit == 'K' || unit == 'k') multiplier = 1024;
            else if (unit == 'M' || unit == 'm') multiplier = 1024 * 1024;
            else if (unit == 'G' || unit == 'g') multiplier = 1024 * 1024 * 1024;
            else return static_cast<size_t>(-1);
        }

        if (!isNumber(numberPart)) return static_cast<size_t>(-1);
        return static_cast<size_t>(std::atol(numberPart.c_str())) * multiplier;
    }

    bool isServerDirectiveWord(const std::string& value) {
        return value == "listen" || value == "server_name" || value == "root" ||
               value == "index" || value == "client_max_body_size" || value == "error_page" ||
               value == "location";
    }

    bool isLocationDirectiveWord(const std::string& value) {
        return value == "allowed_methods" || value == "root" || value == "alias" ||
               value == "autoindex" || value == "index" || value == "return" ||
               value == "cgi_extension" || value == "cgi_path" ||
               value == "cgi_idle_timeout" || value == "cgi_max_timeout" ||
               value == "upload_store" ||
               value == "client_max_body_size" || value == "location" || value == "server";
    }
}

void ConfigParser::requireArgs(const std::vector<std::string>& args, const std::string& directive, size_t minCount) const {
    if (args.size() < minCount) {
        throw ConfigException(directive + " directive missing arguments");
    }
}

std::vector<std::string> ConfigParser::extractArgs(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end, const std::string& directive, bool isServerScope) const {
    std::vector<std::string> args;
    ++it; // Skip the directive word itself
    while (it != end && *it != ";" && *it != "{" && *it != "}") {
        bool invalidWord = isServerScope ? isServerDirectiveWord(*it) : isLocationDirectiveWord(*it);
        if (invalidWord) {
            throw ConfigException("Missing semicolon after directive: " + directive);
        }
        args.push_back(*it);
        ++it;
    }
    
    if (it == end || *it != ";") {
        throw ConfigException("Missing semicolon after directive: " + directive);
    }
    
    return args;
}

std::string ConfigParser::joinPaths(const std::string& path1, const std::string& path2) const {
    if (path1.empty()) return path2;
    if (path2.empty()) return path1;
    
    if (path1[path1.size() - 1] == '/' && path2[0] == '/') {
        return path1 + path2.substr(1);
    } else if (path1[path1.size() - 1] != '/' && path2[0] != '/') {
        return path1 + "/" + path2;
    } else {
        return path1 + path2;
    }
}

void ConfigParser::tokenize(std::ifstream& file) {
    std::string line;
    while (std::getline(file, line)) {
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        std::string token;
        for (size_t i = 0; i < line.length(); ++i) {
            char c = line[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!token.empty()) {
                    _tokens.push_back(token);
                    token.clear();
                }
            } else if (c == '{' || c == '}' || c == ';') {
                if (!token.empty()) {
                    _tokens.push_back(token);
                    token.clear();
                }
                _tokens.push_back(std::string(1, c));
            } else {
                token += c;
            }
        }
        if (!token.empty()) {
            _tokens.push_back(token);
        }
    }
}

void ConfigParser::parseServerBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end) {
    ServerConfig newServer;
    bool listenSeen = false;
    
    // Skip the '{'
    ++it;

    while (it != end && *it != "}") {
        std::string directive = *it;
        if (directive == "location") {
            ++it;
            parseLocationBlock(it, end, newServer);
        } else {
            std::vector<std::string> args = extractArgs(it, end, directive, true);
            
            // Process specific server directives
            if (directive == "listen") {
                requireArgs(args, directive);
                std::string value = args[0];
                uint16_t port;
                size_t colon = value.find(':');
                if (colon != std::string::npos) {
                    std::string host = value.substr(0, colon);
                    std::string portStr = value.substr(colon + 1);
                    if (host.empty() || !isNumber(portStr)) throw ConfigException("Invalid listen value: " + value);
                    newServer.host = host;
                    port = static_cast<uint16_t>(std::atoi(portStr.c_str()));
                } else {
                    if (!isNumber(value)) throw ConfigException("Invalid listen port: " + value);
                    port = static_cast<uint16_t>(std::atoi(value.c_str()));
                }
                // Clear the default port on the first listen directive
                if (!listenSeen) {
                    newServer.listen_ports.clear();
                    listenSeen = true;
                }
                newServer.listen_ports.push_back(port);
            }
            else if (directive == "server_name") {
                newServer.server_names = args;
            }
            else if (directive == "root") {
                requireArgs(args, directive);
                newServer.root = args[0];
            }
            else if (directive == "index") {
                requireArgs(args, directive);
                newServer.index = args[0];
            }
            else if (directive == "client_max_body_size") {
                requireArgs(args, directive);
                size_t parsedSize = parseSizeWithUnit(args[0]);
                if (parsedSize == static_cast<size_t>(-1)) throw ConfigException("Invalid client_max_body_size: " + args[0]);
                newServer.client_max_body_size = parsedSize;
            }
            else if (directive == "error_page") {
                requireArgs(args, directive, 2);
                std::string path = args[args.size() - 1];
                for (size_t i = 0; i + 1 < args.size(); ++i) {
                    if (!isNumber(args[i])) throw ConfigException("Invalid error_page code: " + args[i]);
                    int code = std::atoi(args[i].c_str());
                    newServer.error_pages[code] = path;
                }
            }
            else {
                throw ConfigException("Unknown server directive: " + directive);
            }
            ++it; // Skip ';'
        }
    }
    
    if (it == end) {
        throw ConfigException("Unexpected end of file inside server block");
    }
    ++it; // Skip '}'
    
    // Ensure a default root location '/' exists to catch all unmatched requests
    // (falling back to the server's root config)
    if (newServer.locations.find("/") == newServer.locations.end()) {
        LocationConfig defaultLoc;
        defaultLoc.path = "/";
        defaultLoc.root = newServer.root;
        defaultLoc.index = newServer.index;
        defaultLoc.client_max_body_size = newServer.client_max_body_size;
        defaultLoc.error_pages = newServer.error_pages;
        newServer.locations["/"] = defaultLoc;
    }

    _servers.push_back(newServer);
}

void ConfigParser::parseLocationBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end, ServerConfig& currentServer) {
    LocationConfig newLocation;
    bool hasCustomMaxBody = false;
    
    if (it == end || *it == "{") {
        throw ConfigException("Missing path for location block");
    }
    
    std::string path = *it;
    newLocation.path = path;
    ++it;
    
    if (it == end || *it != "{") {
        throw ConfigException("Expected '{' after location path");
    }
    ++it; // Skip '{'
    
    while (it != end && *it != "}") {
        std::string directive = *it;
        std::vector<std::string> args = extractArgs(it, end, directive, false);
        
        // Process specific location directives
        if (directive == "allowed_methods") {
            requireArgs(args, directive);
            for (size_t j = 0; j < args.size(); ++j) {
                if (args[j] != "GET" && args[j] != "POST" && args[j] != "DELETE") {
                    throw ConfigException("Invalid method in allowed_methods: '" + args[j] +
                                         "' (only GET, POST, DELETE are supported)");
                }
            }
            newLocation.allowed_methods = args;
        } else if (directive == "root") {
            requireArgs(args, directive);
            newLocation.root = args[0];
        } else if (directive == "alias") {
            requireArgs(args, directive);
            newLocation.alias = args[0];
        } else if (directive == "autoindex") {
            requireArgs(args, directive);
            if (args[0] == "on") newLocation.autoindex = true;
            else if (args[0] == "off") newLocation.autoindex = false;
            else throw ConfigException("Invalid autoindex value: " + args[0]);
        } else if (directive == "index") {
            requireArgs(args, directive);
            newLocation.index = args[0];
        } else if (directive == "return") {
            requireArgs(args, directive, 2);
            if (!isNumber(args[0])) throw ConfigException("Invalid return code: " + args[0]);
            newLocation.redirect_code = std::atoi(args[0].c_str());
            newLocation.redirect_url = args[1];
        } else if (directive == "cgi_extension") {
            requireArgs(args, directive);
            newLocation.cgi_extensions = args;
        } else if (directive == "cgi_path") {
            requireArgs(args, directive);
            newLocation.cgi_path = args[0];
        } else if (directive == "cgi_idle_timeout") {
            requireArgs(args, directive);
            if (!isNumber(args[0])) throw ConfigException("Invalid cgi_idle_timeout: " + args[0]);
            newLocation.cgi_idle_timeout = std::atoi(args[0].c_str());
        } else if (directive == "cgi_max_timeout") {
            requireArgs(args, directive);
            if (!isNumber(args[0])) throw ConfigException("Invalid cgi_max_timeout: " + args[0]);
            newLocation.cgi_max_timeout = std::atoi(args[0].c_str());
        } else if (directive == "upload_store") {
            requireArgs(args, directive);
            newLocation.upload_store = args[0];
        } else if (directive == "client_max_body_size") {
            requireArgs(args, directive);
            size_t parsedSize = parseSizeWithUnit(args[0]);
            if (parsedSize == static_cast<size_t>(-1)) throw ConfigException("Invalid client_max_body_size: " + args[0]);
            newLocation.client_max_body_size = parsedSize;
            hasCustomMaxBody = true;
        } else {
            throw ConfigException("Unknown location directive: " + directive);
        }
        
        ++it; // Skip ';'
    }
    
    if (it == end) {
        throw ConfigException("Unexpected end of file inside location block");
    }
    ++it; // Skip '}'

    if (!newLocation.cgi_extensions.empty() && !newLocation.cgi_path.empty()) {
        for (size_t i = 0; i < newLocation.cgi_extensions.size(); ++i) {
            newLocation.cgi_map[newLocation.cgi_extensions[i]] = newLocation.cgi_path;
        }
    }

    // Inherit root from server if location doesn't specify one
    if (newLocation.root.empty()) {
        newLocation.root = currentServer.root;
    }
    // Inherit index from server if location doesn't specify one
    if (newLocation.index.empty()) {
        newLocation.index = currentServer.index;
    }
    // Inherit client_max_body_size from server only if not overridden in location
    if (!hasCustomMaxBody) {
        newLocation.client_max_body_size = currentServer.client_max_body_size;
    }
    // Inherit error_pages from server
    newLocation.error_pages = currentServer.error_pages;
    
    currentServer.locations[path] = newLocation;
}

void ConfigParser::parse() {
    std::ifstream file(_configFilePath.c_str());
    if (!file.is_open()) {
        throw ConfigException("Failed to open config file: " + _configFilePath);
    }
    _tokens.clear();
    _servers.clear();

    tokenize(file);
    file.close();
    
    std::vector<std::string>::iterator it = _tokens.begin();
    while (it != _tokens.end()) {
        if (*it == "server") {
            ++it;
            if (it == _tokens.end() || *it != "{") {
                throw ConfigException("Expected '{' after server directive");
            }
            parseServerBlock(it, _tokens.end());
        } else {
            throw ConfigException("Unknown directive outside server block: " + *it);
        }
    }
}

void ConfigParser::validate() {
    if (_servers.empty()) {
        throw ConfigException("No valid server blocks found in configuration");
    }

    // Check for duplicate host + port pairs across all server blocks
    std::set<std::pair<std::string, uint16_t> > listenPairs;
    for (size_t i = 0; i < _servers.size(); ++i) {
        for (size_t p = 0; p < _servers[i].listen_ports.size(); ++p) {
            std::pair<std::string, uint16_t> listenKey(_servers[i].host, _servers[i].listen_ports[p]);
            if (!listenPairs.insert(listenKey).second) {
                std::ostringstream oss;
                oss << "Duplicate server listen address: " << _servers[i].host << ":" << _servers[i].listen_ports[p];
                throw ConfigException(oss.str());
            }
        }
    }

    for (size_t i = 0; i < _servers.size(); ++i) {
        if (_servers[i].listen_ports.empty()) {
            throw ConfigException("No listen ports in server block");
        }
        for (size_t p = 0; p < _servers[i].listen_ports.size(); ++p) {
            if (_servers[i].listen_ports[p] == 0) {
                throw ConfigException("Invalid listen port in server block");
            }
        }
        if (_servers[i].root.empty()) {
            throw ConfigException("Missing root in server block");
        }

        // Validate error_page paths exist and are regular files
        for (std::map<int, std::string>::const_iterator it = _servers[i].error_pages.begin();
             it != _servers[i].error_pages.end(); ++it) {
            std::string fullPath = _servers[i].root;
            std::string errPath = it->second;

            fullPath = joinPaths(fullPath, errPath);

            struct stat buffer;
            if (stat(fullPath.c_str(), &buffer) != 0) {
                throw ConfigException("Error page file does not exist: " + fullPath);
            }
            if (!S_ISREG(buffer.st_mode)) {
                throw ConfigException("Error page path is not a regular file: " + fullPath);
            }
        }
    }
}

std::vector<ServerConfig> ConfigParser::getServers() const {
    return _servers;
}

std::vector<int> ConfigParser::getPorts() const {
    std::vector<int> ports;
    std::set<int> seen;
    for (size_t i = 0; i < _servers.size(); ++i) {
        for (size_t p = 0; p < _servers[i].listen_ports.size(); ++p) {
            int port = _servers[i].listen_ports[p];
            if (seen.find(port) == seen.end()) {
                seen.insert(port);
                ports.push_back(port);
            }
        }
    }
    return ports;
}
