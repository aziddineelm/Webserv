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

	std::string contentType = req.getHeader("content-type");

	// Route to the right handler based on content type
	if (contentType.find("multipart/form-data") != std::string::npos)
		_saveMultipart(req, loc, contentType, res);
	else
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

void PostHandler::_saveMultipart(const Request &req, const LocationConfig &loc, const std::string &contentType, Response &res) {
	std::string boundary = _extractBoundary(contentType);
	if (boundary.empty()) {
		HttpUtils::buildErrorPage(400, loc, res);
		return;
	}
	std::string fullBoundary = "--" + boundary;
	std::string endMarker   = "\r\n" + fullBoundary;

	std::string bodySource = req.getBodyFilePath();
	bool fromFile = !bodySource.empty();
	std::ifstream bodyFile;
	std::string   bodyString;
	size_t bodySize = 0;
	size_t bodyPos  = 0;

	if (fromFile) {
		bodyFile.open(bodySource.c_str(), std::ios::binary);
		if (!bodyFile.is_open()) {
			HttpUtils::buildErrorPage(500, loc, res);
			return;
		}
		struct stat st;
		if (stat(bodySource.c_str(), &st) != 0) {
			HttpUtils::buildErrorPage(500, loc, res);
			return;
		}
		bodySize = static_cast<size_t>(st.st_size);
	} else {
		bodyString = req.getBody();
		bodySize   = bodyString.size();
	}

	size_t headerBufSize = 4096;
	if (headerBufSize > bodySize)
		headerBufSize = bodySize;
	std::string headerBuf(headerBufSize, '\0');
	if (fromFile) {
		bodyFile.read(&headerBuf[0], headerBufSize);
		headerBuf.resize(static_cast<size_t>(bodyFile.gcount()));
	} else {
		headerBuf = bodyString.substr(0, headerBufSize);
	}

	size_t bndPos = headerBuf.find(fullBoundary);
	if (bndPos == std::string::npos) {
		HttpUtils::buildErrorPage(400, loc, res);
		return;
	}

	size_t headersStart = bndPos + fullBoundary.size() + 2;
	size_t dataStart    = headerBuf.find("\r\n\r\n", headersStart);
	if (dataStart == std::string::npos) {
		HttpUtils::buildErrorPage(400, loc, res);
		return;
	}

	std::string partHeaders = headerBuf.substr(headersStart, dataStart - headersStart);
	std::string filename    = _extractFilenameFromHeaders(partHeaders);
	if (filename.empty()) {
		HttpUtils::buildErrorPage(400, loc, res);
		return;
	}

	size_t lastSlash = filename.rfind('/');
	if (lastSlash != std::string::npos)
		filename = filename.substr(lastSlash + 1);
	size_t lastBs = filename.rfind('\\');
	if (lastBs != std::string::npos)
		filename = filename.substr(lastBs + 1);
	if (filename.empty() || HttpUtils::hasPathTraversal(filename)) {
		HttpUtils::buildErrorPage(400, loc, res); return;
	}

	std::string savePath = loc.upload_store;
	if (savePath[savePath.size() - 1] != '/')
		savePath += "/";
	savePath += filename;

	std::ofstream outFile(savePath.c_str(), std::ios::binary);
	if (!outFile.is_open()) {
		HttpUtils::buildErrorPage(500, loc, res);
		return;
	}

	if (fromFile)
		bodyFile.seekg(static_cast<std::streamoff>(dataStart + 4));
	bodyPos = dataStart + 4;

	std::string carryOver;
	char        readBuf[8192];
	bool        foundEnd = false;

	while (bodyPos < bodySize && !foundEnd) {
		size_t toRead = sizeof(readBuf);
		if (bodyPos + toRead > bodySize)
			toRead = bodySize - bodyPos;

		size_t bytesRead = 0;
		if (fromFile) {
			bodyFile.read(readBuf, toRead);
			bytesRead = static_cast<size_t>(bodyFile.gcount());
		} else {
			bodyString.copy(readBuf, toRead, bodyPos);
			bytesRead = toRead;
		}
		bodyPos += bytesRead;

		std::string combined = carryOver + std::string(readBuf, bytesRead);
		size_t endPos = combined.find(endMarker);
		if (endPos != std::string::npos) {
			outFile.write(combined.c_str(), endPos);
			foundEnd = true;
		} else {
			size_t safeWrite = (combined.size() > endMarker.size()) ? combined.size() - endMarker.size() : 0;
			if (safeWrite > 0)
				outFile.write(combined.c_str(), safeWrite);
			carryOver = combined.substr(safeWrite);
		}
	}
	if (!foundEnd && !carryOver.empty()) {
		outFile.write(carryOver.c_str(), carryOver.size());
	}

	if (outFile.fail() || outFile.bad()) {
		outFile.close();
		HttpUtils::buildErrorPage(500, loc, res);
		return;
	}
	outFile.close();

	res.setStatus(201);
	res.setHeader("Content-Type", "text/plain");
	res.setBody("File uploaded: " + filename);
}

std::string PostHandler::_extractBoundary(const std::string &contentType) {
	std::string marker = "boundary=";
	size_t pos = contentType.find(marker);
	if (pos == std::string::npos)
		return "";

	std::string boundary = contentType.substr(pos + marker.size());
	if (boundary.size() >= 2 && boundary[0] == '"' && boundary[boundary.size() - 1] == '"')
		boundary = boundary.substr(1, boundary.size() - 2);

	size_t end = boundary.find_first_of(" \t;");
	if (end != std::string::npos)
		boundary = boundary.substr(0, end);

	return boundary;
}

std::string PostHandler::_extractFilenameFromHeaders(const std::string &headers) {
	std::string marker = "filename=\"";
	size_t pos = headers.find(marker);
	if (pos == std::string::npos)
		return "";

	pos += marker.size();
	size_t endPos = headers.find('"', pos);
	if (endPos == std::string::npos)
		return "";

	return headers.substr(pos, endPos - pos);
}
