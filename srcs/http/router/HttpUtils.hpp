#ifndef HTTP_UTILS_HPP
#define HTTP_UTILS_HPP

#include <string>
#include "../response/response.hpp"
#include "../../config/ServerConfig.hpp"

class HttpUtils {
public:
	static void			buildErrorPage(int code, const LocationConfig &loc, Response &res);
	static std::string	getExtension(const std::string &path);
	static bool			fileExists(const std::string &path);
	static bool			isDirectory(const std::string &path);
	static bool			hasPathTraversal(const std::string &path);
};

#endif
