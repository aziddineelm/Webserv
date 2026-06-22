#ifndef ROUTER_HPP
#define ROUTER_HPP

#include <string>
#include <vector>
#include <map>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>

#include "../request/request.hpp"
#include "../response/response.hpp"

// ============================================================
// LocationConfig — interface contract with Person C
// ============================================================

struct LocationConfig {
	std::string					path;				// e.g., "/", "/upload"
	std::string					root;				// e.g., "/var/www/html"
	std::string					index;				// e.g., "index.html"
	bool						autoindex;			// directory listing on/off
	std::vector<std::string>	allowed_methods;	// e.g., ["GET", "POST"]
	std::string					redirect_url;		// "" = no redirect
	int							redirect_code;		// 0 = no redirect
	std::string					upload_store;		// "" = uploads not allowed
	size_t						client_max_body_size;// bytes (default 1MB)
	std::string					error_page_dir;		// path to custom error pages

	LocationConfig()
		: autoindex(false),
		  redirect_code(0),
		  client_max_body_size(1048576) {}
};

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
	void	handleRequest(const Request &req, Response &res,
						  const std::vector<LocationConfig> &locations);

private:
	// Step 1: Find the matching location (longest prefix)
	const LocationConfig	*_matchLocation(const std::string &uri,
						const std::vector<LocationConfig> &locations);

	// Step 2: Check if the HTTP method is allowed
	bool	_isMethodAllowed(const std::string &method,
							 const LocationConfig &loc);

	// Step 3: Resolve URI → filesystem path
	std::string	_resolvePath(const std::string &uri,
							 const LocationConfig &loc);

	// Step 4: Serve different resource types
	void	_serveFile(const std::string &filePath, Response &res);
	void	_serveDirectory(const std::string &dirPath,
							const std::string &uri,
							const LocationConfig &loc, Response &res);
	void	_generateDirListing(const std::string &dirPath,
								const std::string &uri, Response &res);

	// Build error page using location's custom dir or fallback
	void	_buildError(int code, const LocationConfig &loc, Response &res);

	// Utility
	static std::string	_getExtension(const std::string &path);
	static bool			_fileExists(const std::string &path);
	static bool			_isDirectory(const std::string &path);
	static bool			_hasPathTraversal(const std::string &path);
};

#endif
