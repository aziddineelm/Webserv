#include "EnvBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
    std::string normalizeHeaderKey(const std::string& name) {
        std::string out;
        out.reserve(name.size());
        for (size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (c == '-') c = '_';
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        return out;
    }

    bool hasKey(const std::map<std::string, std::string>& m, const std::string& key) {
        return m.find(key) != m.end();
    }

}

EnvBuilder::EnvBuilder() {}
EnvBuilder::~EnvBuilder() {}


std::vector<std::string> EnvBuilder::buildEnv(const Request& req, const ServerConfig& serverConfig, const std::string& clientIp) const {
    std::map<std::string, std::string> envMap;

    // 1. Process HTTP Headers -> HTTP_*
    const std::map<std::string, std::string>& headers = req.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        if (!it->second.empty()) {
            std::string envKey = "HTTP_" + normalizeHeaderKey(it->first);
            envMap[envKey] = it->second;
        }
    }

    // 2. Standard CGI Variables
    envMap["REQUEST_METHOD"] = req.getMethod();
    if (!req.getQueryString().empty()) {
        envMap["QUERY_STRING"] = req.getQueryString();
    }
    
    std::string scriptName = req.getPath();
    envMap["SCRIPT_NAME"] = scriptName;
    envMap["PATH_INFO"] = scriptName; // Simple mapping, could be enhanced based on router logic
    
    if (!serverConfig.root.empty()) {
        envMap["PATH_TRANSLATED"] = serverConfig.root + scriptName;
    }
    
    envMap["REQUEST_URI"] = req.getUri();
    envMap["SERVER_PROTOCOL"] = req.getVersion().empty() ? "HTTP/1.1" : req.getVersion();
    
    // Server Name & Port
    std::string srvName = serverConfig.host;
    if (!serverConfig.server_names.empty()) {
        srvName = serverConfig.server_names[0];
    }
    
    std::map<std::string, std::string>::const_iterator hostIt = headers.find("host");
    if (hostIt != headers.end() && !hostIt->second.empty()) {
        std::string hostVal = hostIt->second;
        size_t colonPos = hostVal.find(':');
        if (colonPos != std::string::npos) {
            envMap["SERVER_NAME"] = hostVal.substr(0, colonPos);
            envMap["SERVER_PORT"] = hostVal.substr(colonPos + 1);
        } else {
            envMap["SERVER_NAME"] = hostVal;
        }
    } else {
        envMap["SERVER_NAME"] = srvName;
    }
    
    if (!hasKey(envMap, "SERVER_PORT")) {
        std::ostringstream portStr;
        if (!serverConfig.listen_ports.empty()) {
            portStr << serverConfig.listen_ports[0];
        }
        envMap["SERVER_PORT"] = portStr.str();
    }

    if (!clientIp.empty()) {
        envMap["REMOTE_ADDR"] = clientIp;
    }

    // Content Length & Type
    std::map<std::string, std::string>::const_iterator clIt = headers.find("content-length");
    if (clIt != headers.end() && !clIt->second.empty()) {
        envMap["CONTENT_LENGTH"] = clIt->second;
    }

    std::map<std::string, std::string>::const_iterator ctIt = headers.find("content-type");
    if (ctIt != headers.end() && !ctIt->second.empty()) {
        envMap["CONTENT_TYPE"] = ctIt->second;
    }

    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_SOFTWARE"] = SERVER_SOFTWARE_VERSION;
    envMap["REDIRECT_STATUS"] = "200";

    // Script Filename for PHP-CGI
    if (!serverConfig.root.empty()) {
        envMap["SCRIPT_FILENAME"] = serverConfig.root + scriptName;
    }

    // 3. Convert Map to Vector of Strings
    std::vector<std::string> env;
    env.reserve(envMap.size());
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin(); it != envMap.end(); ++it) {
        env.push_back(it->first + "=" + it->second);
    }
    
    return env;
}
