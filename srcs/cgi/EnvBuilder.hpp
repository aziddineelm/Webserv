#ifndef ENVBUILDER_HPP
#define ENVBUILDER_HPP

#include <string>
#include <vector>

#define SERVER_SOFTWARE_VERSION "webserv/1.0"
#include <map>
#include "../http/request/request.hpp"
#include "../config/ServerConfig.hpp"


class EnvBuilder {
public:
    EnvBuilder();
    ~EnvBuilder();
    std::vector<std::string> buildEnv(const Request& req, const ServerConfig& serverConfig, const std::string& clientIp) const;
};

#endif
