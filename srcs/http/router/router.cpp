#include "router.hpp"
#include "HttpUtils.hpp"
#include "PostHandler.hpp"
#include "GetHandler.hpp"
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
			HttpUtils::buildErrorPage(code, *errLoc, res);
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
		HttpUtils::buildErrorPage(405, *loc, res);
		return;
	}

	// 4. Enforce client_max_body_size
	if (loc->client_max_body_size > 0
		&& req.getBodyBytesWritten() > loc->client_max_body_size) {
		HttpUtils::buildErrorPage(413, *loc, res);
		return;
	}

	// 5. Route by method — POST and DELETE have their own handlers
	if (req.getMethod() == "DELETE") {
		_handleDelete(req, *loc, res);
		return;
	}
	if (req.getMethod() == "POST") {
		PostHandler::handle(req, *loc, res);
		return;
	}

	// 5. GET: Resolve URI → filesystem path
	std::string filePath = _resolvePath(req.getPath(), *loc);

	// 5b. Block path traversal
	if (filePath.empty() || HttpUtils::hasPathTraversal(filePath)) {
		HttpUtils::buildErrorPage(403, *loc, res);
		return;
	}

	// 6. Check for CGI dispatch
	std::string ext = HttpUtils::getExtension(filePath);
	if (!ext.empty() && loc->cgi_map.find(ext) != loc->cgi_map.end()) {
		// TODO: Dispatch to CGIHandler (Person C will wire this up)
		HttpUtils::buildErrorPage(501, *loc, res);
		return;
	}

	// 7. Serve
	GetHandler::handle(req, *loc, filePath, res);
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
// Private: DELETE Handler
// ============================================================

void Router::_handleDelete(const Request &req, const LocationConfig &loc, Response &res) {
	std::string filePath = _resolvePath(req.getPath(), loc);

	if (filePath.empty() || HttpUtils::hasPathTraversal(filePath)) {
		HttpUtils::buildErrorPage(403, loc, res);
		return;
	}

	// Cannot delete directories
	if (HttpUtils::isDirectory(filePath)) {
		HttpUtils::buildErrorPage(403, loc, res);
		return;
	}

	if (!HttpUtils::fileExists(filePath)) {
		HttpUtils::buildErrorPage(404, loc, res);
		return;
	}

	if (unlink(filePath.c_str()) != 0) {
		HttpUtils::buildErrorPage(500, loc, res);
		return;
	}

	res.setStatus(200);
	res.setHeader("Content-Type", "text/plain");
	res.setBody("File deleted successfully");
}
