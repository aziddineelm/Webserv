#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <string>
#include <map>
#include <sstream>
#include <fstream>

class Response {

public:
	Response();
	Response(const Response &other);
	Response &operator=(const Response &other);
	~Response();

	// Builder methods
	void	setStatus(int code);
	void	setHeader(const std::string &key, const std::string &value);
	void	setBody(const std::string &body);

	// Convenience builders
	void	buildErrorPage(int code, const std::string &filePath = "");
	void	buildRedirect(int code, const std::string &location);

	// Serialize — turn the Response into raw bytes
	std::string	serialize() const;

	// Getters
	int				getStatusCode() const;
	std::string		getHeader(const std::string &key) const;
	const std::string&	getBody() const;

	// Utility (static)
	static std::string	getMimeType(const std::string &extension);
	static std::string	getReasonPhrase(int code);

private:
	int				_statusCode;
	std::string		_reasonPhrase;
	std::map<std::string, std::string>	_headers;
	std::string		_body;

	// Fallback error page + file reader
	static std::string	_generateErrorHtml(int code, const std::string &reason);
	static bool			_readFile(const std::string &path, std::string &content);
};

#endif