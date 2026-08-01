#ifndef ENVBUILDER_HPP
#define ENVBUILDER_HPP

#include <string>
#include <vector>

#define SERVER_SOFTWARE_VERSION "webserv/1.0"
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

    // Build environment variables directly from a Request object and server config.
    // This allows perfect integration with the HTTP and Router layers.
    std::vector<std::string> buildEnv(const Request& req, const ServerConfig& serverConfig, const std::string& clientIp) const;
};

#endif
