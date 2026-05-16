// TODO: checking that error_page paths actually exist AND Throwing errors on duplicate server listen + host pairs (so bind() doesn't fail later) AND Enforce that allowed_methods strictly only accepts permutations of GET, POST, and DELETE
#include "ConfigParser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>

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
                size_t colon = value.find(':');
                if (colon != std::string::npos) {
                    std::string host = value.substr(0, colon);
                    std::string portStr = value.substr(colon + 1);
                    if (host.empty() || !isNumber(portStr)) throw ConfigException("Invalid listen value: " + value);
                    newServer.host = host;
                    newServer.listen_port = static_cast<uint16_t>(std::atoi(portStr.c_str()));
                } else {
                    if (!isNumber(value)) throw ConfigException("Invalid listen port: " + value);
                    newServer.listen_port = static_cast<uint16_t>(std::atoi(value.c_str()));
                }
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
    for (size_t i = 0; i < _servers.size(); ++i) {
        if (_servers[i].listen_port == 0) {
            throw ConfigException("Invalid listen port in server block");
        }
        if (_servers[i].root.empty()) {
            throw ConfigException("Missing root in server block");
        }
    }
}

std::vector<ServerConfig> ConfigParser::getServers() const {
    return _servers;
}
