#include <cstddef>
#include <iostream>
#include <string>
#include <map>

class Request {

	public:
		Request();
		Request(const Request& other);
		Request& operator=(const Request& other);
		~Request();
		std::string feed(const std::string& data);
		bool isComplete() const;
		bool hasError() const;
		std::string getMethod();
		std::string getUri() const;
		std::string getPath() const;
		std::string getQueryString() const;
		std::string getVersion() const;
		std::string getHeaderValue(const std::string& key);
		std::string getHeaders() const;
		std::string getBody() const;
		int getErrorCode() const;

	private:
		std::string _method;
		std::string _uri;
		std::string _path;
		std::string _queruString;
		std::string _version;
		std::map<std::string, std::string> _headers;
		std::string _body;
		std::string _rawBuffer;
		size_t _contertLength;
		bool _isChunked;
		int _errorCode;
		enum _state { 
			REQUES_LINE,
			HEADERS,
			BODY,
			CHUNKED_BODY,
			COMPLETE,
			ERROR
		};
		_state _currentState;
};	