#ifndef GET_HANDLER_HPP
#define GET_HANDLER_HPP

#include <string>
#include "../request/request.hpp"
#include "../response/response.hpp"
#include "../../config/ServerConfig.hpp"

class GetHandler {
public:
	static void handle(const Request &req, const LocationConfig &loc, const std::string &filePath, Response &res);

private:
	static void _serveFile(const std::string &filePath, Response &res);
	static void _serveDirectory(const std::string &dirPath, const std::string &uri, const LocationConfig &loc, Response &res);
	static void _generateDirListing(const std::string &dirPath, const std::string &uri, Response &res);
};

#endif
