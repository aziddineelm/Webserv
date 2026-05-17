#include "request.hpp"

Request::Request() {}

Request::Request(const Request& other) {}

Request& Request::operator=(const Request& other) {
	if (this != &other) {
		this->_method = other._method;
		this->_uri = other._uri;
		this->_path = other._path;
		this->_queruString = other._queruString;
		this->_version = other._version;
		this->_headers = other._headers;
		this->_body = other._body;
		this->_rawBuffer = other._rawBuffer;
		this->_contertLength = other._contertLength;
		this->_isChunked = other._isChunked;
		this->_errorCode = other._errorCode;
		this->_currentState = other._currentState;
	}
	return *this;
}

Request::~Request() {}

