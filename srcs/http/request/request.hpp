#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>
#include <map>
#include <cstdlib>
#include <sstream>

enum ParseState {
	REQUEST_LINE,
	HEADERS,
	BODY,
	CHUNKED_BODY,
	COMPLETE,
	ERROR
};

class Request {

public:
	Request();
	~Request();

	// Core interface
	void	feed(const std::string& data);
	bool	isComplete() const;
	bool	hasError() const;
	void	reset();

	// Getters
	const std::string&	getMethod() const;
	const std::string&	getUri() const;
	const std::string&	getPath() const;
	const std::string&	getQueryString() const;
	const std::string&	getVersion() const;
	std::string			getHeader(const std::string& key) const;
	const std::map<std::string, std::string>&	getHeaders() const;
	const std::string&	getBody() const;
	int					getErrorCode() const;
	ParseState			getState() const;

private:
	// Parsed data
	std::string		_method;
	std::string		_uri;
	std::string		_path;
	std::string		_queryString;
	std::string		_version;
	std::map<std::string, std::string>	_headers;
	std::string		_body;

	// Parser state
	ParseState		_state;
	std::string		_buffer;
	size_t			_contentLength;
	bool			_isChunked;
	int				_errorCode;

	// State machine steps (each does ONE thing)
	void	_parseRequestLine();
	void	_parseHeaders();
	void	_parseBody();
	void	_parseChunkedBody();

	// Sub-tasks extracted for SRP
	bool	_skipLeadingCRLF();
	bool	_extractLine(std::string& line);
	bool	_splitRequestLine(const std::string& line);
	void	_splitUri();
	bool	_extractHeaderBlock(std::string& headerBlock);
	bool	_parseHeaderLine(const std::string& line);
	void	_validateHeaders();
	void	_decideBodyState();

	// Error
	void	_setError(int code);

	// Utility (DRY helpers)
	static std::string	_toLower(const std::string& str);
	static std::string	_trim(const std::string& str);
	static bool			_parseNumber(const std::string& str, long& result, int base);
};

#endif