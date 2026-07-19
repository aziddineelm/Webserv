#ifndef ROUTER_HPP
#define ROUTER_HPP

#include <string>
#include <vector>

#include "../request/request.hpp"
#include "../response/response.hpp"
#include "../../config/ServerConfig.hpp"

// ============================================================
// Router — the brain that connects Request → Response
// ============================================================

class Router {

public:
	Router();
	Router(const Router &other);
	Router &operator=(const Router &other);
	~Router();

	// The ONE method Person A calls after request is complete
	void handleRequest(const Request &req, Response &res, const std::vector<LocationConfig> &locations);

	// Step 1: Find the matching location (longest prefix)
	static const LocationConfig* matchLocation(const std::string &uri, const std::vector<LocationConfig> &locations);

	// Phase 5: Virtual Hosting — resolve the correct ServerConfig for a request.
	// Must be called AFTER request is complete (Host header is available).
	// Returns NULL only if no server block listens on listenPort at all.
	static const ServerConfig* resolveVirtualHost(const Request &req, int listenPort, const std::vector<ServerConfig> &configs);

private:
	// Step 2: Check if the HTTP method is allowed
	bool _isMethodAllowed(const std::string &method, const LocationConfig &loc);

	// Step 3: Resolve URI → filesystem path
	std::string _resolvePath(const std::string &uri, const LocationConfig &loc);

	// Step 5: DELETE handler
	void _handleDelete(const std::string &filePath, const LocationConfig &loc, Response &res);
};

#endif
