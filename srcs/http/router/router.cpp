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
// Core Routing Handler
// ============================================================

void Router::handleRequest(const Request &req, Response &res, const std::vector<LocationConfig> &locations) {
	// ── Step 1: Match Location (Longest Prefix) ──
	const LocationConfig *loc = matchLocation(req.getPath(), locations);

	// If the request itself has errors, respond immediately
	if (req.hasError()) {
		int code = req.getErrorCode();
		if (code == 0)
			code = 400;
		
		if (loc)
			HttpUtils::buildErrorPage(code, *loc, res);
		else
			res.buildErrorPage(code);
		return;
	}

	if (!loc) {
		res.buildErrorPage(404);
		return;
	}

	// ── Step 2: Check Redirect ──
	if (loc->redirect_code != 0 && !loc->redirect_url.empty()) {
		res.buildRedirect(loc->redirect_code, loc->redirect_url);
		return;
	}

	// ── Step 3: Check Allowed Methods ──
	if (!_isMethodAllowed(req.getMethod(), *loc)) {
		HttpUtils::buildErrorPage(405, *loc, res);
		return;
	}

	// ── Step 4: Enforce client_max_body_size ──
	if (loc->client_max_body_size > 0
		&& req.getBodyBytesWritten() > loc->client_max_body_size) {
		HttpUtils::buildErrorPage(413, *loc, res);
		return;
	}

	// ── Step 5: Resolve URI to Filesystem Path ──
	std::string filePath = _resolvePath(req.getPath(), *loc);

	if (filePath.empty() || HttpUtils::hasPathTraversal(filePath)) {
		HttpUtils::buildErrorPage(403, *loc, res);
		return;
	}

	// ── Step 6: Route Request (CGI vs Static Method) ──
	std::string ext = HttpUtils::getExtension(filePath);
	if (!ext.empty() && loc->cgi_map.find(ext) != loc->cgi_map.end()) {
		// CGI dispatch (handles GET, POST, etc.)
		res.setCgiScript(filePath, loc->cgi_map.find(ext)->second);
	} else if (req.getMethod() == "DELETE") {
		_handleDelete(filePath, *loc, res);
	} else if (req.getMethod() == "POST") {
		PostHandler::handle(req, *loc, res);
	} else {
		// Serve GET static file or directory
		GetHandler::handle(req, *loc, filePath, res);
	}

}

// ============================================================
// Private: Location Matching — Longest Prefix Wins
// ============================================================

const LocationConfig *Router::matchLocation(const std::string &uri, const std::vector<LocationConfig> &locations) {
	const LocationConfig *best = NULL;
	size_t bestLen = 0;

	for (size_t i = 0; i < locations.size(); ++i) {
		const std::string &prefix = locations[i].path;

		// URI must start with the location path
		if (uri.compare(0, prefix.size(), prefix) == 0) {
			// For non-root locations, ensure we match at a boundary
			// e.g., /images should NOT match /img
			if (prefix != "/" && prefix.size() < uri.size() && uri[prefix.size()] != '/')
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
// Path Resolution (URI to Filesystem)
// ============================================================

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

void Router::_handleDelete(const std::string &filePath, const LocationConfig &loc, Response &res) {
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

// ============================================================
// Virtual Host Resolution (NGINX Algorithm)
// ============================================================

const ServerConfig *Router::resolveVirtualHost(const Request &req, int listenPort, const std::vector<ServerConfig> &configs)
{
	// Extract and normalize Host header — strip optional ":port" suffix
	std::string host = req.getHeader("host");
	size_t colonPos = host.rfind(':');
	if (colonPos != std::string::npos)
		host = host.substr(0, colonPos);

	const ServerConfig *fallback = NULL;

	for (size_t i = 0; i < configs.size(); ++i) {
		// Check if this config listens on the requested port
		bool listensOnPort = false;
		for (size_t p = 0; p < configs[i].listen_ports.size(); ++p) {
			if (configs[i].listen_ports[p] == static_cast<uint16_t>(listenPort)) {
				listensOnPort = true;
				break;
			}
		}
		if (!listensOnPort)
			continue;

		// First port-matching config is the default fallback
		if (fallback == NULL)
			fallback = &configs[i];

		// Exact server_name match — return immediately, no need to keep searching
		for (size_t n = 0; n < configs[i].server_names.size(); ++n) {
			if (configs[i].server_names[n] == host)
				return &configs[i];
		}
	}

	// No server_name matched — NGINX returns first port-matching config
	return fallback;
}
