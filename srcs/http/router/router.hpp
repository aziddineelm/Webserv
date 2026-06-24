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
