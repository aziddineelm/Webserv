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
};

#endif
