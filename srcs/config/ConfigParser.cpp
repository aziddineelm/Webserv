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
        if (value.empty()) return 0;
        char unit = value[value.size() - 1];
        std::string numberPart = value;
        size_t multiplier = 1;

        if (!std::isdigit(static_cast<unsigned char>(unit))) {
            numberPart = value.substr(0, value.size() - 1);
            if (unit == 'K' || unit == 'k') multiplier = 1024;
            else if (unit == 'M' || unit == 'm') multiplier = 1024 * 1024;
            else if (unit == 'G' || unit == 'g') multiplier = 1024 * 1024 * 1024;
            else return 0;
        }

        if (!isNumber(numberPart)) return 0;
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
               value == "cgi_extension" || value == "cgi_path" || value == "upload_store" ||
               value == "location" || value == "server";
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
            // Read until ';'
            std::vector<std::string> args;
            ++it;
            while (it != end && *it != ";" && *it != "{" && *it != "}") {
                if (isServerDirectiveWord(*it)) {
                    throw ConfigException("Missing semicolon after directive: " + directive);
                }
                args.push_back(*it);
                ++it;
            }
            
            if (it == end || *it != ";") {
                throw ConfigException("Missing semicolon after directive: " + directive);
            }
            
            // Process specific server directives
            if (directive == "listen") {
                if (args.empty()) throw ConfigException("listen directive missing arguments");
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
                if (args.empty()) throw ConfigException("root directive missing arguments");
                newServer.root = args[0];
            }
            else if (directive == "index") {
                if (args.empty()) throw ConfigException("index directive missing arguments");
                newServer.index = args[0];
            }
            else if (directive == "client_max_body_size") {
                if (args.empty()) throw ConfigException("client_max_body_size directive missing arguments");
                size_t parsedSize = parseSizeWithUnit(args[0]);
                if (parsedSize == 0) throw ConfigException("Invalid client_max_body_size: " + args[0]);
                newServer.client_max_body_size = parsedSize;
            }
            else if (directive == "error_page") {
                if (args.size() < 2) throw ConfigException("error_page directive missing arguments");
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
    
    _servers.push_back(newServer);
}

void ConfigParser::parseLocationBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end, ServerConfig& currentServer) {
    LocationContext newLocation;
    
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
        std::vector<std::string> args;
        ++it;
        
        while (it != end && *it != ";" && *it != "{" && *it != "}") {
            if (isLocationDirectiveWord(*it)) {
                throw ConfigException("Missing semicolon after location directive: " + directive);
            }
            args.push_back(*it);
            ++it;
        }
        
        if (it == end || *it != ";") {
            throw ConfigException("Missing semicolon after location directive: " + directive);
        }
        
        // Process specific location directives
        if (directive == "allowed_methods") {
            if (args.empty()) throw ConfigException("allowed_methods directive missing arguments");
            for (size_t j = 0; j < args.size(); ++j) {
                if (args[j] != "GET" && args[j] != "POST" && args[j] != "DELETE") {
                    throw ConfigException("Invalid method in allowed_methods: '" + args[j] +
                                         "' (only GET, POST, DELETE are supported)");
                }
            }
            newLocation.allowed_methods = args;
        } else if (directive == "root") {
            if (args.empty()) throw ConfigException("root directive missing arguments");
            newLocation.root = args[0];
        } else if (directive == "alias") {
            if (args.empty()) throw ConfigException("alias directive missing arguments");
            newLocation.alias = args[0];
        } else if (directive == "autoindex") {
            if (args.empty()) throw ConfigException("autoindex directive missing arguments");
            if (args[0] == "on") newLocation.autoindex = true;
            else if (args[0] == "off") newLocation.autoindex = false;
            else throw ConfigException("Invalid autoindex value: " + args[0]);
        } else if (directive == "index") {
            if (args.empty()) throw ConfigException("index directive missing arguments");
            newLocation.index = args[0];
        } else if (directive == "return") {
            if (args.size() < 2) throw ConfigException("return directive missing arguments");
            if (!isNumber(args[0])) throw ConfigException("Invalid return code: " + args[0]);
            newLocation.redirect.first = std::atoi(args[0].c_str());
            newLocation.redirect.second = args[1];
        } else if (directive == "cgi_extension") {
            if (args.empty()) {
                throw ConfigException("cgi_extension directive missing arguments");
            }
            newLocation.cgi_extensions = args;
            if (!newLocation.cgi_path.empty()) {
                for (size_t i = 0; i < args.size(); ++i) {
                    newLocation.cgi_map[args[i]] = newLocation.cgi_path;
                }
            }
        } else if (directive == "cgi_path") {
            if (args.empty()) throw ConfigException("cgi_path directive missing arguments");
            newLocation.cgi_path = args[0];
            if (!newLocation.cgi_extensions.empty()) {
                for (size_t i = 0; i < newLocation.cgi_extensions.size(); ++i) {
                    newLocation.cgi_map[newLocation.cgi_extensions[i]] = newLocation.cgi_path;
                }
            }
        } else if (directive == "upload_store") {
            if (args.empty()) throw ConfigException("upload_store directive missing arguments");
            newLocation.upload_store = args[0];
        } else {
            throw ConfigException("Unknown location directive: " + directive);
        }
        
        ++it; // Skip ';'
    }
    
    if (it == end) {
        throw ConfigException("Unexpected end of file inside location block");
    }
    ++it; // Skip '}'

    // Inherit root from server if location doesn't specify one
    if (newLocation.root.empty()) {
        newLocation.root = currentServer.root;
    }
    // Inherit index from server if location doesn't specify one
    if (newLocation.index.empty()) {
        newLocation.index = currentServer.index;
    }
    
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

            if (!fullPath.empty() && !errPath.empty()) {
                if (fullPath[fullPath.size() - 1] == '/' && errPath[0] == '/') {
                    fullPath += errPath.substr(1);
                } else if (fullPath[fullPath.size() - 1] != '/' && errPath[0] != '/') {
                    fullPath += "/" + errPath;
                } else {
                    fullPath += errPath;
                }
            } else {
                fullPath += errPath;
            }

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
