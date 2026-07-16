#ifndef POST_HANDLER_HPP
#define POST_HANDLER_HPP

#include <string>
#include "../request/request.hpp"
#include "../response/response.hpp"
#include "../../config/ServerConfig.hpp"

class PostHandler {
public:
	static void handle(const Request &req, const LocationConfig &loc, Response &res);

private:
	static void _saveRawBody(const Request &req, const LocationConfig &loc, Response &res);
	static void _saveMultipart(const Request &req, const LocationConfig &loc, const std::string &contentType, Response &res);
	static std::string _extractBoundary(const std::string &contentType);
	static std::string _extractFilenameFromHeaders(const std::string &headers);
};

#endif
