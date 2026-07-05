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

// Bonus: Extract a specific cookie value from a raw Cookie header.
// Input:  "theme=dark; session_id=abc123; lang=en"
// Call:   extractCookieValue(header, "session_id") -> "abc123"
std::string HttpUtils::extractCookieValue(const std::string &cookieHeader, const std::string &key) {
	if (cookieHeader.empty() || key.empty())
		return "";

	std::string search = key + "=";
	size_t pos = 0;

	while (pos < cookieHeader.size()) {
		// Skip leading whitespace
		while (pos < cookieHeader.size() && (cookieHeader[pos] == ' ' || cookieHeader[pos] == ';'))
			++pos;

		// Check if this token starts with our key
		if (cookieHeader.compare(pos, search.size(), search) == 0) {
			size_t valueStart = pos + search.size();
			size_t valueEnd = cookieHeader.find(';', valueStart);
			if (valueEnd == std::string::npos)
				valueEnd = cookieHeader.size();
			// Trim trailing whitespace from value
			while (valueEnd > valueStart && cookieHeader[valueEnd - 1] == ' ')
				--valueEnd;
			return cookieHeader.substr(valueStart, valueEnd - valueStart);
		}

		// Skip to next cookie pair
		pos = cookieHeader.find(';', pos);
		if (pos == std::string::npos)
			break;
		++pos;
	}
	return "";
}

// Bonus: Returns true for common static asset extensions (skip session logic for performance)
bool HttpUtils::isStaticAsset(const std::string &path) {
	std::string ext = getExtension(path);
	if (ext.empty())
		return false;
	return (ext == ".css" || ext == ".js" || ext == ".png" || ext == ".jpg"
		|| ext == ".jpeg" || ext == ".gif" || ext == ".ico" || ext == ".svg"
		|| ext == ".woff" || ext == ".woff2" || ext == ".ttf" || ext == ".mp4"
		|| ext == ".webm" || ext == ".mp3" || ext == ".pdf" || ext == ".zip");
}

