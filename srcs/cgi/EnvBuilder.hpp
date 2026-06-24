#ifndef ENVBUILDER_HPP
#define ENVBUILDER_HPP

#include <string>
#include <vector>
#include <map>
#include "../http/request/request.hpp"
#include "../config/ServerConfig.hpp"

// EnvBuilder constructs CGI environment variables from request
// metadata and server/location configuration. This stub returns a
// vector of strings in the form "KEY=VALUE".
class EnvBuilder {
public:
    EnvBuilder();
    ~EnvBuilder();

    // Build environment variables given a basic map of request values.
    // The actual implementation should translate request headers, method,
    // URI, query string, server info, and other CGI-required variables.
    std::vector<std::string> build(const std::map<std::string, std::string>& requestMeta) const;

    // Build environment variables from common request parts.
    // Headers are provided as name/value pairs; they will be converted
    // to HTTP_* entries where appropriate.
    std::vector<std::string> buildFromParts(const std::string& method,
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
                                            const std::string& contentType) const;

    // Build environment variables directly from a Request object and server config.
    // This allows perfect integration with the HTTP and Router layers.
    std::vector<std::string> buildFromRequest(const Request& req, const ServerConfig& serverConfig) const;
};

#endif
