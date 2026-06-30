#include "GetHandler.hpp"
#include "HttpUtils.hpp"
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

void GetHandler::handle(const Request &req, const LocationConfig &loc, const std::string &filePath, Response &res) {
	if (HttpUtils::isDirectory(filePath)) {
		_serveDirectory(filePath, req.getPath(), loc, res);
	} else if (HttpUtils::fileExists(filePath)) {
		_serveFile(filePath, res);
	} else {
		HttpUtils::buildErrorPage(404, loc, res);
	}
}

void GetHandler::_serveFile(const std::string &filePath, Response &res) {
	struct stat st;
	if (stat(filePath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		res.buildErrorPage(404);
		return;
	}

	if (access(filePath.c_str(), R_OK) != 0) {
		res.buildErrorPage(403);
		return;
	}

	res.setStatus(200);
	res.setHeader("Content-Type", Response::getMimeType(HttpUtils::getExtension(filePath)));
	res.setFilePath(filePath, static_cast<size_t>(st.st_size));
}

void GetHandler::_serveDirectory(const std::string &dirPath, const std::string &uri, const LocationConfig &loc, Response &res) {
	if (uri.empty() || uri[uri.size() - 1] != '/') {
		res.buildRedirect(301, uri + "/");
		return;
	}

	if (!loc.index.empty()) {
		std::string indexPath = dirPath;
		if (indexPath[indexPath.size() - 1] != '/') indexPath += "/";
		indexPath += loc.index;

		if (HttpUtils::fileExists(indexPath)) {
			_serveFile(indexPath, res);
			return;
		}
	}

	if (loc.autoindex) {
		_generateDirListing(dirPath, uri, res);
	} else {
		HttpUtils::buildErrorPage(403, loc, res);
	}
}

void GetHandler::_generateDirListing(const std::string &dirPath, const std::string &uri, Response &res) {
	DIR *dir = opendir(dirPath.c_str());
	if (!dir) {
		res.buildErrorPage(500);
		return;
	}

	std::ostringstream oss;
	oss << "<html>\n<head><title>Index of " << uri << "</title></head>\n<body>\n"
		<< "<h1>Index of " << uri << "</h1>\n<hr>\n<ul>\n";

	std::vector<std::string> entries;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		if (name == ".") continue;
		entries.push_back(name);
	}
	closedir(dir);

	for (size_t i = 0; i < entries.size(); ++i) {
		for (size_t j = i + 1; j < entries.size(); ++j) {
			if (entries[j] < entries[i]) {
				std::string tmp = entries[i];
				entries[i] = entries[j];
				entries[j] = tmp;
			}
		}
	}

	for (size_t i = 0; i < entries.size(); ++i) {
		std::string fullEntryPath = dirPath;
		if (!fullEntryPath.empty() && fullEntryPath[fullEntryPath.size() - 1] != '/')
			fullEntryPath += "/";
		fullEntryPath += entries[i];
		bool isDir = HttpUtils::isDirectory(fullEntryPath);

		std::string displayName = entries[i];
		if (isDir) displayName += "/";

		oss << "<li><a href=\"" << entries[i];
		if (isDir) oss << "/";
		oss << "\">" << displayName << "</a></li>\n";
	}

	oss << "</ul>\n<hr>\n<p>webserv</p>\n</body>\n</html>\n";

	res.setStatus(200);
	res.setHeader("Content-Type", "text/html");
	res.setBody(oss.str());
}
