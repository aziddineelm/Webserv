#include "PostHandler.hpp"
#include "HttpUtils.hpp"
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

void PostHandler::handle(const Request &req, const LocationConfig &loc, Response &res) {
	if (loc.upload_store.empty()) {
		HttpUtils::buildErrorPage(403, loc, res);
		return;
	}

	_saveRawBody(req, loc, res);
}

void PostHandler::_saveRawBody(const Request &req, const LocationConfig &loc, Response &res) {
	std::string filename = req.getPath();
	size_t slash = filename.rfind('/');
	if (slash != std::string::npos)
		filename = filename.substr(slash + 1);
	if (filename.empty() || HttpUtils::hasPathTraversal(filename)) {
		HttpUtils::buildErrorPage(400, loc, res);
		return;
	}

	std::string savePath = loc.upload_store;
	if (savePath[savePath.size() - 1] != '/')
		savePath += "/";
	savePath += filename;

	if (!req.getBodyFilePath().empty()) {
		if (std::rename(req.getBodyFilePath().c_str(), savePath.c_str()) != 0) {
			// Fallback: EXDEV (cross-device link) rename() fails across different filesystems (e.g. /tmp to /var)
			std::ifstream src(req.getBodyFilePath().c_str(), std::ios::binary);
			std::ofstream dst(savePath.c_str(), std::ios::binary);
			if (!src.is_open() || !dst.is_open()) {
				HttpUtils::buildErrorPage(500, loc, res);
				return;
			}
			dst << src.rdbuf();
			src.close();
			dst.close();
			unlink(req.getBodyFilePath().c_str());
		}
	} else {
		std::ofstream out(savePath.c_str(), std::ios::binary);
		if (!out.is_open()) {
			HttpUtils::buildErrorPage(500, loc, res);
			return;
		}
		out.write(req.getBody().c_str(), req.getBody().size());
		if (out.fail() || out.bad()) {
			out.close();
			HttpUtils::buildErrorPage(500, loc, res);
			return;
		}
		out.close();
	}

	res.setStatus(201);
	res.setHeader("Content-Type", "text/plain");
	res.setBody("File uploaded: " + filename);
}
