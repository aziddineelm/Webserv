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

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool hasKey(const std::map<std::string, std::string>& m, const std::string& key) {
    return m.find(key) != m.end();
}

std::string getFirst(const std::map<std::string, std::string>& m,
                     const std::string& k1,
                     const std::string& k2 = std::string(),
                     const std::string& k3 = std::string()) {
    std::map<std::string, std::string>::const_iterator it = m.find(k1);
    if (it != m.end()) return it->second;
    if (!k2.empty()) {
        it = m.find(k2);
        if (it != m.end()) return it->second;
    }
    if (!k3.empty()) {
        it = m.find(k3);
        if (it != m.end()) return it->second;
    }
    return std::string();
}
}

EnvBuilder::EnvBuilder() {}
EnvBuilder::~EnvBuilder() {}

std::vector<std::string> EnvBuilder::build(const std::map<std::string, std::string>& requestMeta) const {
    std::map<std::string, std::string> envMap;

    for (std::map<std::string, std::string>::const_iterator it = requestMeta.begin(); it != requestMeta.end(); ++it) {
        const std::string& key = it->first;
        const std::string& value = it->second;
        if (startsWith(key, "header:") || startsWith(key, "http_header:") || startsWith(key, "http:")) {
            std::string headerName = key.substr(key.find(':') + 1);
            std::string envKey = "HTTP_" + normalizeHeaderKey(headerName);
            if (!value.empty() && !hasKey(envMap, envKey)) {
                envMap[envKey] = value;
            }
        } else if (startsWith(key, "HTTP_")) {
            if (!value.empty()) envMap[key] = value;
        }
    }

    const std::string method = getFirst(requestMeta, "method", "REQUEST_METHOD");
    if (!method.empty()) envMap["REQUEST_METHOD"] = method;

    const std::string query = getFirst(requestMeta, "query", "query_string", "QUERY_STRING");
    if (!query.empty()) envMap["QUERY_STRING"] = query;

    const std::string scriptName = getFirst(requestMeta, "script_name", "SCRIPT_NAME");
    if (!scriptName.empty()) envMap["SCRIPT_NAME"] = scriptName;

    const std::string pathInfo = getFirst(requestMeta, "path_info", "PATH_INFO");
    if (!pathInfo.empty()) {
        envMap["PATH_INFO"] = pathInfo;
        // PATH_TRANSLATED = document_root + PATH_INFO (RFC 3875 §4.1.6)
        const std::string docRoot = getFirst(requestMeta, "document_root", "root", "DOCUMENT_ROOT");
        if (!docRoot.empty()) {
            envMap["PATH_TRANSLATED"] = docRoot + pathInfo;
        }
    }

    const std::string requestUri = getFirst(requestMeta, "request_uri", "REQUEST_URI");
    if (!requestUri.empty()) envMap["REQUEST_URI"] = requestUri;

    const std::string serverProtocol = getFirst(requestMeta, "server_protocol", "SERVER_PROTOCOL");
    if (!serverProtocol.empty()) envMap["SERVER_PROTOCOL"] = serverProtocol;

    const std::string serverName = getFirst(requestMeta, "server_name", "SERVER_NAME");
    if (!serverName.empty()) envMap["SERVER_NAME"] = serverName;

    const std::string serverPort = getFirst(requestMeta, "server_port", "SERVER_PORT");
    if (!serverPort.empty()) envMap["SERVER_PORT"] = serverPort;

    const std::string remoteAddr = getFirst(requestMeta, "remote_addr", "REMOTE_ADDR");
    if (!remoteAddr.empty()) envMap["REMOTE_ADDR"] = remoteAddr;

    const std::string contentLength = getFirst(requestMeta, "content_length", "Content-Length", "CONTENT_LENGTH");
    if (!contentLength.empty()) envMap["CONTENT_LENGTH"] = contentLength;

    const std::string contentType = getFirst(requestMeta, "content_type", "Content-Type", "CONTENT_TYPE");
    if (!contentType.empty()) envMap["CONTENT_TYPE"] = contentType;

    const std::string host = getFirst(requestMeta, "host", "Host");
    if (!host.empty()) {
        envMap["HTTP_HOST"] = host;
        if (!hasKey(envMap, "SERVER_NAME")) {
            size_t pos = host.find(':');
            if (pos == std::string::npos) envMap["SERVER_NAME"] = host;
            else envMap["SERVER_NAME"] = host.substr(0, pos);
        }
        if (!hasKey(envMap, "SERVER_PORT")) {
            size_t pos = host.find(':');
            if (pos != std::string::npos && pos + 1 < host.size()) {
                envMap["SERVER_PORT"] = host.substr(pos + 1);
            }
        }
    }

    const std::string userAgent = getFirst(requestMeta, "user_agent", "User-Agent", "HTTP_USER_AGENT");
    if (!userAgent.empty()) envMap["HTTP_USER_AGENT"] = userAgent;

    if (!hasKey(envMap, "GATEWAY_INTERFACE")) envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    if (!hasKey(envMap, "SERVER_SOFTWARE")) {
        const std::string serverSoftware = getFirst(requestMeta, "server_software", "SERVER_SOFTWARE");
        envMap["SERVER_SOFTWARE"] = serverSoftware.empty() ? "webserv" : serverSoftware;
    }

    std::vector<std::string> env;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin(); it != envMap.end(); ++it) {
        env.push_back(it->first + std::string("=") + it->second);
    }
    return env;
}

std::vector<std::string> EnvBuilder::buildFromParts(const std::string& method,
                                                    const std::string& requestUri,
                                                    const std::string& queryString,
                                                    const std::map<std::string, std::string>& headers,
                                                    const std::string& serverName,
                                                    const std::string& serverPort,
                                                    const std::string& remoteAddr,
                                                    const std::string& scriptName,
                                                    const std::string& pathInfo,
                                                    const std::string& serverProtocol,
                                                    const std::string& contentLength,
                                                    const std::string& contentType) const {
    std::map<std::string, std::string> meta;

    if (!method.empty()) meta["method"] = method;
    if (!requestUri.empty()) meta["request_uri"] = requestUri;
    if (!queryString.empty()) meta["query"] = queryString;
    if (!serverName.empty()) meta["server_name"] = serverName;
    if (!serverPort.empty()) meta["server_port"] = serverPort;
    if (!remoteAddr.empty()) meta["remote_addr"] = remoteAddr;
    if (!scriptName.empty()) meta["script_name"] = scriptName;
    if (!pathInfo.empty()) meta["path_info"] = pathInfo;
    if (!serverProtocol.empty()) meta["server_protocol"] = serverProtocol;
    if (!contentLength.empty()) meta["content_length"] = contentLength;
    if (!contentType.empty()) meta["content_type"] = contentType;

    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        if (!it->first.empty()) meta["header:" + it->first] = it->second;
    }

    return build(meta);
}

std::vector<std::string> EnvBuilder::buildFromRequest(const Request& req, const ServerConfig& serverConfig) const {
    std::ostringstream portStr;
    if (!serverConfig.listen_ports.empty()) {
        portStr << serverConfig.listen_ports[0];
    }

    std::string contentLength;
    std::string contentType;
    const std::map<std::string, std::string>& headers = req.getHeaders();

    std::map<std::string, std::string>::const_iterator clIt = headers.find("content-length");
    if (clIt != headers.end()) contentLength = clIt->second;

    std::map<std::string, std::string>::const_iterator ctIt = headers.find("content-type");
    if (ctIt != headers.end()) contentType = ctIt->second;

    // Determine server_name: use first configured name, or fallback to host
    std::string srvName;
    if (!serverConfig.server_names.empty()) {
        srvName = serverConfig.server_names[0];
    } else {
        srvName = serverConfig.host;
    }

    return buildFromParts(
        req.getMethod(),
        req.getUri(),
        req.getQueryString(),
        headers,
        srvName,
        portStr.str(),
        "",             // remoteAddr — only Person A knows this from accept()
        req.getPath(),  // scriptName
        req.getPath(),  // pathInfo — RFC 3875 / 42 cgi_tester require PATH_INFO
        req.getVersion(),
        contentLength,
        contentType
    );
}
