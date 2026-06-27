#include "router.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unistd.h>
// ============================================================
// Orthodox Canonical Form
// ============================================================

Router::Router() {}

Router::Router(const Router &other) { (void)other; }

Router &Router::operator=(const Router &other) { (void)other; return *this; }

Router::~Router() {}

// ============================================================
// Core: handleRequest — the ONE method Person A calls
// ============================================================
//
// Decision flow:
//   1. Match location (longest prefix)
//   2. Check redirect
//   3. Check method allowed
//   4. Resolve path
//   5. Route by method (POST/DELETE/GET)
//   6. Check CGI dispatch
//   7. Serve file / directory / 404
//

void Router::handleRequest(const Request &req, Response &res, const std::vector<LocationConfig> &locations) {
	// If the request itself has errors, respond immediately
	if (req.hasError()) {
		int code = req.getErrorCode();
		if (code == 0)
			code = 400;
		// No location matched yet, try to find one for error page
		const LocationConfig *errLoc = _matchLocation(req.getPath(), locations);
		if (errLoc)
			_buildError(code, *errLoc, res);
		else
			res.buildErrorPage(code);
		return;
	}

	// 1. Match location
	const LocationConfig *loc = _matchLocation(req.getPath(), locations);
	if (!loc) {
		res.buildErrorPage(404);
		return;
	}

	// 2. Check redirect
	if (loc->redirect_code != 0 && !loc->redirect_url.empty()) {
		res.buildRedirect(loc->redirect_code, loc->redirect_url);
		return;
	}

	// 3. Check method allowed
	if (!_isMethodAllowed(req.getMethod(), *loc)) {
		_buildError(405, *loc, res);
		return;
	}

	// 4. Enforce client_max_body_size
	if (loc->client_max_body_size > 0
		&& req.getBodyBytesWritten() > loc->client_max_body_size) {
		_buildError(413, *loc, res);
		return;
	}

	// 5. Route by method — POST and DELETE have their own handlers
	if (req.getMethod() == "DELETE") {
		_handleDelete(req, *loc, res);
		return;
	}
	if (req.getMethod() == "POST") {
		_handlePost(req, *loc, res);
		return;
	}

	// 5. GET: Resolve URI → filesystem path
	std::string filePath = _resolvePath(req.getPath(), *loc);

	// 5b. Block path traversal
	if (filePath.empty() || _hasPathTraversal(filePath)) {
		_buildError(403, *loc, res);
		return;
	}

	// 6. Check for CGI dispatch
	std::string ext = _getExtension(filePath);
	if (!ext.empty() && loc->cgi_map.find(ext) != loc->cgi_map.end()) {
		// TODO: Dispatch to CGIHandler (Person C will wire this up)
		_buildError(501, *loc, res);
		return;
	}

	// 7. Serve
	if (_isDirectory(filePath)) {
		_serveDirectory(filePath, req.getPath(), *loc, res);
	} else if (_fileExists(filePath)) {
		_serveFile(filePath, res);
	} else {
		_buildError(404, *loc, res);
	}
}

// ============================================================
// Private: Location Matching — Longest Prefix Wins
// ============================================================

const LocationConfig *Router::_matchLocation( const std::string &uri, const std::vector<LocationConfig> &locations) {
	const LocationConfig *best = NULL;
	size_t bestLen = 0;

	for (size_t i = 0; i < locations.size(); ++i) {
		const std::string &prefix = locations[i].path;

		// URI must start with the location path
		if (uri.compare(0, prefix.size(), prefix) == 0) {
			// For non-root locations, ensure we match at a boundary
			// e.g., /images should NOT match /img
			if (prefix != "/" && prefix.size() < uri.size()
				&& uri[prefix.size()] != '/')
				continue;

			// Longest prefix wins
			if (prefix.size() > bestLen) {
				bestLen = prefix.size();
				best = &locations[i];
			}
		}
	}
	return best;
}

// ============================================================
// Private: Method Enforcement
// ============================================================

bool Router::_isMethodAllowed(const std::string &method, const LocationConfig &loc) {
	// Empty list means all methods allowed
	if (loc.allowed_methods.empty())
		return true;

	for (size_t i = 0; i < loc.allowed_methods.size(); ++i) {
		if (loc.allowed_methods[i] == method)
			return true;
	}
	return false;
}

// ============================================================
// Private: Path Resolution — URI → Filesystem
// ============================================================
//
// Subject requirement (line 170–173):
//   if URL /kapouet is rooted to /tmp/www,
//   URL /kapouet/pouic/toto/pouet will search for
//   /tmp/www/pouic/toto/pouet
//

std::string Router::_resolvePath(const std::string &uri, const LocationConfig &loc) {
	// Alias replaces the location prefix entirely with a different path
	// Root appends the remainder after stripping the prefix
	if (!loc.alias.empty()) {
		// alias: /kapouet → /tmp/www means /kapouet/foo → /tmp/www/foo
		std::string remainder;
		if (uri.size() > loc.path.size())
			remainder = uri.substr(loc.path.size());

		std::string fullPath = loc.alias;
		if (!fullPath.empty() && fullPath[fullPath.size() - 1] == '/'
			&& !remainder.empty() && remainder[0] == '/')
			fullPath = fullPath.substr(0, fullPath.size() - 1);
		else if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/'
				 && !remainder.empty() && remainder[0] != '/')
			fullPath += "/";

		fullPath += remainder;
		return fullPath;
	}

	if (loc.root.empty())
		return "";

	// Strip the location prefix from the URI
	std::string remainder;
	if (loc.path == "/") {
		remainder = uri;
	} else {
		remainder = uri.substr(loc.path.size());
	}

	// Build filesystem path: root + remainder
	std::string fullPath = loc.root;

	// Ensure proper slash joining
	if (!fullPath.empty() && fullPath[fullPath.size() - 1] == '/'
		&& !remainder.empty() && remainder[0] == '/') {
		// Both have slash — remove one
		fullPath = fullPath.substr(0, fullPath.size() - 1);
	} else if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/'
			   && !remainder.empty() && remainder[0] != '/') {
		// Neither has slash — add one
		fullPath += "/";
	}

	fullPath += remainder;
	return fullPath;
}

// ============================================================
// Private: Serve a Static File (Streaming — Phase 4)
// ============================================================

void Router::_serveFile(const std::string &filePath, Response &res) {
	// stat() tells us if it exists, is a regular file, and its size
	struct stat st;
	if (stat(filePath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		res.buildErrorPage(404);
		return;
	}

	// Check read permission by trying to open (then close immediately — no data loaded)
	std::ifstream testOpen(filePath.c_str(), std::ios::binary);
	if (!testOpen.is_open()) {
		res.buildErrorPage(403);
		return;
	}
	testOpen.close();

	// Set file-mode streaming — no data loaded into RAM
	res.setStatus(200);
	res.setHeader("Content-Type", Response::getMimeType(_getExtension(filePath)));
	res.setFilePath(filePath, static_cast<size_t>(st.st_size));
}

// ============================================================
// Private: Serve a Directory
// ============================================================
//
// Priority:
//   1. If URI lacks trailing slash → redirect 301 to add it
//   2. If index file exists → serve it
//   3. If autoindex is on → generate directory listing
//   4. Else → 403 Forbidden
//

void Router::_serveDirectory(const std::string &dirPath, const std::string &uri, const LocationConfig &loc, Response &res) {
	// 1. Redirect if no trailing slash
	if (uri.empty() || uri[uri.size() - 1] != '/') {
		res.buildRedirect(301, uri + "/");
		return;
	}

	// 2. Try serving the index file
	if (!loc.index.empty()) {
		std::string indexPath = dirPath;
		if (!indexPath.empty() && indexPath[indexPath.size() - 1] != '/')
			indexPath += "/";
		indexPath += loc.index;

		if (_fileExists(indexPath)) {
			_serveFile(indexPath, res);
			return;
		}
	}

	// 3. Try autoindex
	if (loc.autoindex) {
		_generateDirListing(dirPath, uri, res);
		return;
	}

	// 4. No index, no autoindex → forbidden
	_buildError(403, loc, res);
}

// ============================================================
// Private: Generate Directory Listing HTML (autoindex)
// ============================================================

void Router::_generateDirListing(const std::string &dirPath, const std::string &uri, Response &res) {
	DIR *dir = opendir(dirPath.c_str());
	if (!dir) {
		res.buildErrorPage(500);
		return;
	}

	std::ostringstream oss;
	oss << "<html>\n"
		<< "<head><title>Index of " << uri << "</title></head>\n"
		<< "<body>\n"
		<< "<h1>Index of " << uri << "</h1>\n"
		<< "<hr>\n<ul>\n";

	// Collect entries and sort for consistent output
	std::vector<std::string> entries;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		if (name == ".")
			continue;
		entries.push_back(name);
	}
	closedir(dir);

	// Sort entries alphabetically
	for (size_t i = 0; i < entries.size(); ++i) {
		for (size_t j = i + 1; j < entries.size(); ++j) {
			if (entries[j] < entries[i]) {
				std::string tmp = entries[i];
				entries[i] = entries[j];
				entries[j] = tmp;
			}
		}
	}

	// Build links
	for (size_t i = 0; i < entries.size(); ++i) {
		// Check if entry is a directory (cache to avoid double stat())
		std::string fullEntryPath = dirPath;
		if (!fullEntryPath.empty() && fullEntryPath[fullEntryPath.size() - 1] != '/')
			fullEntryPath += "/";
		fullEntryPath += entries[i];
		bool isDir = _isDirectory(fullEntryPath);

		std::string displayName = entries[i];
		if (isDir)
			displayName += "/";

		oss << "<li><a href=\"" << entries[i];
		if (isDir)
			oss << "/";
		oss << "\">" << displayName << "</a></li>\n";
	}

	oss << "</ul>\n<hr>\n"
		<< "<p>webserv</p>\n"
		<< "</body>\n</html>\n";

	res.setStatus(200);
	res.setHeader("Content-Type", "text/html");
	res.setBody(oss.str());
}

// ============================================================
// Private: POST Handler — Thin Orchestrator
// ============================================================

void Router::_handlePost(const Request &req, const LocationConfig &loc, Response &res) {
	if (loc.upload_store.empty()) {
		_buildError(403, loc, res);
		return;
	}

	std::string contentType = req.getHeader("content-type");

	// Route to the right handler based on content type
	if (contentType.find("multipart/form-data") != std::string::npos)
		_saveMultipart(req, loc, contentType, res);
	else
		_saveRawBody(req, loc, res);
}

// ============================================================
// Private: Save Raw Body (non-multipart POST)
// ============================================================
//
// The body IS the file. Filename comes from URL last segment.
// Uses rename() for O(1) file move — zero RAM, zero copying.
//

void Router::_saveRawBody(const Request &req, const LocationConfig &loc, Response &res) {
	// Get filename from URL path last segment
	std::string filename = req.getPath();
	size_t slash = filename.rfind('/');
	if (slash != std::string::npos)
		filename = filename.substr(slash + 1);
	if (filename.empty()) {
		_buildError(400, loc, res);
		return;
	}

	// Sanitize — block path traversal in filename
	if (filename.find("..") != std::string::npos) {
		_buildError(400, loc, res);
		return;
	}

	std::string savePath = loc.upload_store;
	if (savePath[savePath.size() - 1] != '/')
		savePath += "/";
	savePath += filename;

	// Body is already on disk as a temp file — just rename it
	// rename() is O(1): no data copying, no RAM, works for files of any size
	if (!req.getBodyFilePath().empty()) {
		if (std::rename(req.getBodyFilePath().c_str(), savePath.c_str()) != 0) {
			_buildError(500, loc, res);
			return;
		}
	} else {
		// Small body still in _body string — write it directly
		std::ofstream out(savePath.c_str(), std::ios::binary);
		if (!out.is_open()) {
			_buildError(500, loc, res);
			return;
		}
		out.write(req.getBody().c_str(), req.getBody().size());
		out.close();
	}

	res.setStatus(201);
	res.setHeader("Content-Type", "text/plain");
	res.setBody("File uploaded: " + filename);
}

// ============================================================
// Private: Save Multipart Body (multipart/form-data POST)
// ============================================================
//
// Streams file data from body temp file → upload destination.
// RAM usage: ~12KB regardless of file size (4KB header + 8KB chunk).
//

void Router::_saveMultipart(const Request &req, const LocationConfig &loc, const std::string &contentType, Response &res) {
	// 1. Extract boundary
	std::string boundary = _extractBoundary(contentType);
	if (boundary.empty()) {
		_buildError(400, loc, res);
		return;
	}
	std::string fullBoundary = "--" + boundary;
	std::string endMarker   = "\r\n" + fullBoundary;

	// 2. Open body source
	std::string bodySource = req.getBodyFilePath();
	bool fromFile = !bodySource.empty();
	std::ifstream bodyFile;
	std::string   bodyString;
	size_t bodySize = 0;
	size_t bodyPos  = 0;

	if (fromFile) {
		bodyFile.open(bodySource.c_str(), std::ios::binary);
		if (!bodyFile.is_open()) { _buildError(500, loc, res); return; }
		struct stat st;
		if (stat(bodySource.c_str(), &st) != 0) {
			_buildError(500, loc, res);
			return;
		}
		bodySize = static_cast<size_t>(st.st_size);
	} else {
		bodyString = req.getBody();
		bodySize   = bodyString.size();
	}

	// 3. Phase A: read 4KB header block to get filename
	size_t headerBufSize = 4096;
	if (headerBufSize > bodySize) headerBufSize = bodySize;
	std::string headerBuf(headerBufSize, '\0');
	if (fromFile) {
		bodyFile.read(&headerBuf[0], headerBufSize);
		headerBuf.resize(static_cast<size_t>(bodyFile.gcount()));
	} else {
		headerBuf = bodyString.substr(0, headerBufSize);
	}

	size_t bndPos = headerBuf.find(fullBoundary);
	if (bndPos == std::string::npos) { _buildError(400, loc, res); return; }

	size_t headersStart = bndPos + fullBoundary.size() + 2;
	size_t dataStart    = headerBuf.find("\r\n\r\n", headersStart);
	if (dataStart == std::string::npos) { _buildError(400, loc, res); return; }

	std::string partHeaders = headerBuf.substr(headersStart, dataStart - headersStart);
	std::string filename    = _extractFilenameFromHeaders(partHeaders);
	if (filename.empty()) { _buildError(400, loc, res); return; }

	// Sanitize filename — strip path components to prevent traversal
	size_t lastSlash = filename.rfind('/');
	if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
	size_t lastBs = filename.rfind('\\');
	if (lastBs != std::string::npos) filename = filename.substr(lastBs + 1);
	if (filename.empty() || filename == ".." || filename == ".") {
		_buildError(400, loc, res); return;
	}

	// 4. Phase B: stream file data → upload destination
	std::string savePath = loc.upload_store;
	if (savePath[savePath.size() - 1] != '/') savePath += "/";
	savePath += filename;

	std::ofstream outFile(savePath.c_str(), std::ios::binary);
	if (!outFile.is_open()) { _buildError(500, loc, res); return; }

	if (fromFile)
		bodyFile.seekg(static_cast<std::streamoff>(dataStart + 4));
	bodyPos = dataStart + 4;

	std::string carryOver;
	char        readBuf[8192];
	bool        foundEnd = false;

	while (bodyPos < bodySize && !foundEnd) {
		size_t toRead = sizeof(readBuf);
		if (bodyPos + toRead > bodySize) toRead = bodySize - bodyPos;

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
			size_t safeWrite = (combined.size() > endMarker.size())
							   ? combined.size() - endMarker.size() : 0;
			if (safeWrite > 0) outFile.write(combined.c_str(), safeWrite);
			carryOver = combined.substr(safeWrite);
		}
	}
	if (!foundEnd && !carryOver.empty())
		outFile.write(carryOver.c_str(), carryOver.size());
	outFile.close();

	res.setStatus(201);
	res.setHeader("Content-Type", "text/plain");
	res.setBody("File uploaded: " + filename);
}

// ============================================================
// Private: DELETE Handler
// ============================================================

void Router::_handleDelete(const Request &req, const LocationConfig &loc, Response &res) {
	std::string filePath = _resolvePath(req.getPath(), loc);

	if (filePath.empty() || _hasPathTraversal(filePath)) {
		_buildError(403, loc, res);
		return;
	}

	// Cannot delete directories
	if (_isDirectory(filePath)) {
		_buildError(403, loc, res);
		return;
	}

	if (!_fileExists(filePath)) {
		_buildError(404, loc, res);
		return;
	}

	if (unlink(filePath.c_str()) != 0) {
		_buildError(500, loc, res);
		return;
	}

	res.setStatus(200);
	res.setHeader("Content-Type", "text/plain");
	res.setBody("File deleted successfully");
}

// ============================================================
// Private: Build Error with Custom Page Support
// ============================================================

void Router::_buildError(int code, const LocationConfig &loc, Response &res) {
	// Check if Person C configured a custom error page for this status code
	std::map<int, std::string>::const_iterator it = loc.error_pages.find(code);
	if (it != loc.error_pages.end() && !it->second.empty()) {
		res.buildErrorPage(code, it->second);
	} else {
		// Fallback to generated HTML
		res.buildErrorPage(code);
	}
}

// ============================================================
// Private: Multipart Helpers
// ============================================================

// Extract boundary from: "multipart/form-data; boundary=----WebKitFormBoundary..."
std::string Router::_extractBoundary(const std::string &contentType) {
	std::string marker = "boundary=";
	size_t pos = contentType.find(marker);
	if (pos == std::string::npos)
		return "";

	std::string boundary = contentType.substr(pos + marker.size());

	// Remove optional quotes
	if (boundary.size() >= 2 && boundary[0] == '"'
		&& boundary[boundary.size() - 1] == '"')
		boundary = boundary.substr(1, boundary.size() - 2);

	// Trim whitespace/semicolons
	size_t end = boundary.find_first_of(" \t;");
	if (end != std::string::npos)
		boundary = boundary.substr(0, end);

	return boundary;
}

// Extract filename from part headers like:
// Content-Disposition: form-data; name="file"; filename="photo.jpg"
std::string Router::_extractFilenameFromHeaders(const std::string &headers) {
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

// ============================================================
// Private: Utility Helpers
// ============================================================

std::string Router::_getExtension(const std::string &path) {
	size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return "";
	// Don't return extension if dot is in a directory name
	size_t slash = path.rfind('/');
	if (slash != std::string::npos && dot < slash)
		return "";
	return path.substr(dot);
}

bool Router::_fileExists(const std::string &path) {
	struct stat st;
	return (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
}

bool Router::_isDirectory(const std::string &path) {
	struct stat st;
	return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

bool Router::_hasPathTraversal(const std::string &path) {
	// Block any path containing ".." to prevent directory traversal attacks
	if (path.find("..") != std::string::npos)
		return true;
	return false;
}
