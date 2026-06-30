#include "HttpUtils.hpp"
#include <sys/stat.h>

void HttpUtils::buildErrorPage(int code, const LocationConfig &loc, Response &res) {
	std::map<int, std::string>::const_iterator it = loc.error_pages.find(code);
	if (it != loc.error_pages.end() && !it->second.empty()) {
		res.buildErrorPage(code, it->second);
	} else {
		res.buildErrorPage(code);
	}
}

std::string HttpUtils::getExtension(const std::string &path) {
	size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return "";
	size_t slash = path.rfind('/');
	if (slash != std::string::npos && dot < slash)
		return "";
	return path.substr(dot);
}

bool HttpUtils::fileExists(const std::string &path) {
	struct stat st;
	return (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
}

bool HttpUtils::isDirectory(const std::string &path) {
	struct stat st;
	return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

bool HttpUtils::hasPathTraversal(const std::string &path) {
	return (path.find("..") != std::string::npos);
}
