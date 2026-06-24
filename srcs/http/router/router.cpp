#include "router.hpp"

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
//   5. Serve file / directory / 404
//

void Router::handleRequest(const Request &req, Response &res, const std::vector<LocationConfig> &locations) {
	// If the request itself has errors, respond immediately
	if (req.hasError()) {
		int code = req.getErrorCode();
		if (code == 0)
			code = 400;
		// No location matched yet, try to find one for error page dir
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

	// 4. Resolve URI → filesystem path
	std::string filePath = _resolvePath(req.getPath(), *loc);

	// 4b. Block path traversal
	if (filePath.empty() || _hasPathTraversal(filePath)) {
		_buildError(403, *loc, res);
		return;
	}

	// 5. Check for CGI dispatch
	std::string ext = _getExtension(filePath);
	if (!ext.empty() && loc->cgi_map.find(ext) != loc->cgi_map.end()) {
		// TODO: Dispatch to CGIHandler (Person C will wire this up)
		_buildError(501, *loc, res);
		return;
	}

	// 6. Serve
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
// Private: Serve a Static File
// ============================================================

void Router::_serveFile(const std::string &filePath, Response &res) {
	// Open in binary mode (important for images, PDFs, etc.)
	std::ifstream file(filePath.c_str(), std::ios::binary);
	if (!file.is_open()) {
		// File exists (stat said so) but can't open → permission denied
		res.buildErrorPage(403);
		return;
	}

	// Read entire file into string
	std::ostringstream oss;
	oss << file.rdbuf();
	if (file.fail()) {
		res.buildErrorPage(500);
		return;
	}

	// Build the response
	res.setStatus(200);
	res.setBody(oss.str());
	res.setHeader("Content-Type", Response::getMimeType(_getExtension(filePath)));
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
