#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <string>
#include <map>

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

	// File streaming — sets file mode instead of loading into RAM
	void	setFilePath(const std::string &path, size_t fileSize);

	// Convenience builders
	void	buildErrorPage(int code, const std::string &filePath = "");
	void	buildRedirect(int code, const std::string &location);

	// Streaming API — Person A calls these in a loop
	std::string	getHeaders() const;
	std::string	getNextChunk();
	bool		isDone() const;

	// Legacy serialize — still works for string-mode responses
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

	// File streaming state (no ifstream member — stays OCF copyable)
	std::string		_filePath;
	size_t			_fileOffset;
	size_t			_fileSize;
	bool			_headersSent;
	bool			_done;

	// Fallback error page + file reader
	static std::string	_generateErrorHtml(int code, const std::string &reason);
	static bool			_readFile(const std::string &path, std::string &content);
};

#endif