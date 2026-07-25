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
	int		getStatusCode() const;
	const std::string &getBody() const;

	// File streaming — sets file mode instead of loading into RAM
	void	setFilePath(const std::string &path, size_t fileSize);

	// Convenience builders
	void	buildErrorPage(int code, const std::string &filePath = "");
	void	buildRedirect(int code, const std::string &location);

	// CGI Live Streaming — builds headers-only response from pre-parsed CGI headers
	void	buildFromCgiHeaders(const std::map<std::string, std::string> &cgiHeaders);
	void	markDone();

	// CGI metadata — Router sets these when the request targets a CGI script
	void		setCgiScript(const std::string &script, const std::string &interpreter);
	std::string	getCgiScript() const;
	std::string	getCgiInterpreter() const;
	bool		isCgi() const;

	// Streaming API — Person A calls these in a loop
	std::string	getHeaders() const;
	std::string	getNextChunk();
	bool		isDone() const;

	// Utility (static)
	static std::string	getMimeType(const std::string &extension);
	static std::string	getReasonPhrase(int code);
	static std::string	formatChunk(const std::string &data);

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
	bool			_isChunked;
	bool			_cgiEOF;

	// CGI metadata (set by Router, read by EventLoop)
	std::string		_cgiScriptPath;
	std::string		_cgiInterpreterPath;

	// Fallback error page + file reader
	static std::string	_generateErrorHtml(int code, const std::string &reason);
	static bool			_readFile(const std::string &path, std::string &content);
};

#endif