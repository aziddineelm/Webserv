#include "request.hpp"

Request::Request() : _state(REQUEST_LINE), _contentLength(0), _isChunked(false), _errorCode(0) {}

Request::~Request() {}

// ============================================================
// Core Interface
// ============================================================

void Request::feed(const std::string &data) {
  if (_state == COMPLETE || _state == ERROR)
    return;
  _buffer.append(data);

  // Advance the state machine as far as possible.
  // Track both state and buffer size — some ops (skip CRLF)
  // consume data without changing state.
  ParseState prevState;
  size_t prevBufSize;
  do {
    prevState = _state;
    prevBufSize = _buffer.size();

    if (_state == REQUEST_LINE)
      _parseRequestLine();
    if (_state == HEADERS)
      _parseHeaders();
    if (_state == BODY)
      _parseBody();
    if (_state == CHUNKED_BODY)
      _parseChunkedBody();

  } while ((_state != prevState || _buffer.size() != prevBufSize) &&
           _state != COMPLETE && _state != ERROR);
}

bool Request::isComplete() const { return _state == COMPLETE; }

bool Request::hasError() const { return _state == ERROR; }

// Clear parsed data for keep-alive reuse.
// Does NOT clear _buffer — leftover bytes belong to the next request.
void Request::reset() {
  _method.clear();
  _uri.clear();
  _path.clear();
  _queryString.clear();
  _version.clear();
  _headers.clear();
  _body.clear();
  _state = REQUEST_LINE;
  _contentLength = 0;
  _isChunked = false;
  _errorCode = 0;
}

// ============================================================
// Getters
// ============================================================

const std::string &Request::getMethod() const { return _method; }
const std::string &Request::getUri() const { return _uri; }
const std::string &Request::getPath() const { return _path; }
const std::string &Request::getQueryString() const { return _queryString; }
const std::string &Request::getVersion() const { return _version; }
std::string Request::getHeader(const std::string &key) const {
  std::map<std::string, std::string>::const_iterator it =
      _headers.find(_toLower(key));
  if (it != _headers.end())
    return it->second;
  return "";
}
const std::map<std::string, std::string> &Request::getHeaders() const {
  return _headers;
}
const std::string &Request::getBody() const { return _body; }
int Request::getErrorCode() const { return _errorCode; }
ParseState Request::getState() const { return _state; }

// ============================================================
// Private: Error + Utility
// ============================================================

void Request::_setError(int code) {
  _state = ERROR;
  _errorCode = code;
}

std::string Request::_toLower(const std::string &str) {
  std::string result = str;
  for (size_t i = 0; i < result.size(); i++)
    result[i] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
  return result;
}

std::string Request::_trim(const std::string &str) {
  size_t start = str.find_first_not_of(" \t");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t");
  return str.substr(start, end - start + 1);
}

// DRY: Parse a numeric string (base 10 or 16) with full validation.
// Returns false if the string is empty, contains non-numeric chars, or is
// negative.
bool Request::_parseNumber(const std::string &str, long &result, int base) {
  std::string trimmed = _trim(str);
  if (trimmed.empty())
    return false;
  char *endPtr = 0;
  result = std::strtol(trimmed.c_str(), &endPtr, base);
  if (endPtr == trimmed.c_str() || *endPtr != '\0' || result < 0)
    return false;
  return true;
}

// ============================================================
// Private: Sub-tasks (SRP — each does one thing)
// ============================================================

// Skip leading CRLFs before the request line (RFC 2616 §4.1).
// Returns true if data was consumed (caller should retry).
bool Request::_skipLeadingCRLF() {
  if (_buffer.size() >= 2 && _buffer[0] == '\r' && _buffer[1] == '\n') {
    _buffer.erase(0, 2);
    return true;
  }
  return false;
}

// Extract the first line from the buffer (up to \r\n).
// Returns false if no complete line is available yet.
// On success, erases the line + \r\n from the buffer.
bool Request::_extractLine(std::string &line) {
  size_t pos = _buffer.find("\r\n");
  if (pos == std::string::npos)
    return false;
  line = _buffer.substr(0, pos);
  _buffer.erase(0, pos + 2);
  return true;
}

// Validate and split "METHOD URI VERSION" into the 3 fields.
// Returns false on any validation failure (sets error).
bool Request::_splitRequestLine(const std::string &line) {
  std::istringstream iss(line);
  std::string method, uri, version, extra;
  iss >> method >> uri >> version;

  if (method.empty() || uri.empty() || version.empty() || (iss >> extra)) {
    _setError(400);
    return false;
  }
  if (method != "GET" && method != "POST" && method != "DELETE") {
    _setError(400);
    return false;
  }
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    _setError(400);
    return false;
  }

  _method = method;
  _uri = uri;
  _version = version;
  return true;
}

// Split stored _uri into _path and _queryString at '?'.
void Request::_splitUri() {
  size_t qpos = _uri.find('?');
  if (qpos != std::string::npos) {
    _path = _uri.substr(0, qpos);
    _queryString = _uri.substr(qpos + 1);
  } else {
    _path = _uri;
    _queryString.clear();
  }
}

// Extract the full header block from the buffer.
// Returns false if headers are not yet complete.
// Handles the edge case of no headers (buffer starts with \r\n).
bool Request::_extractHeaderBlock(std::string &headerBlock) {
  size_t pos = _buffer.find("\r\n\r\n");
  if (pos != std::string::npos) {
    headerBlock = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 4);
    return true;
  }
  // Edge case: no headers at all (blank line immediately after request line)
  if (_buffer.size() >= 2 && _buffer[0] == '\r' && _buffer[1] == '\n') {
    headerBlock.clear();
    _buffer.erase(0, 2);
    return true;
  }
  return false;
}

// Parse a single "Key: Value" line into the _headers map.
// Returns false on malformed header (sets error).
bool Request::_parseHeaderLine(const std::string &line) {
  size_t colonPos = line.find(':');
  if (colonPos == std::string::npos) {
    _setError(400);
    return false;
  }
	if (colonPos > 0 && std::isspace(static_cast<unsigned char>(line[colonPos - 1]))) {
    _setError(400);
    return false;
  }
  std::string key = _toLower(_trim(line.substr(0, colonPos)));
  std::string value = _trim(line.substr(colonPos + 1));
  if (key.empty()) {
    _setError(400);
    return false;
  }
  _headers[key] = value;
  return true;
}

// Validate headers after parsing (Host requirement, etc).
// Sets error on failure.
void Request::_validateHeaders() {
  // HTTP/1.1 requires Host header
  if (_version == "HTTP/1.1") {
    std::map<std::string, std::string>::const_iterator it = _headers.find("host");
    if (it == _headers.end() || it->second.empty()) {
      _setError(400);
      return;
    }
  }

  // Parse Content-Length
  std::map<std::string, std::string>::const_iterator clIt = _headers.find("content-length");
  if (clIt != _headers.end()) {
    long cl;
    if (!_parseNumber(clIt->second, cl, 10)) {
      _setError(400);
      return;
    }
    _contentLength = static_cast<size_t>(cl);
  }

  // Check Transfer-Encoding: chunked
  std::map<std::string, std::string>::const_iterator teIt = _headers.find("transfer-encoding");
  if (teIt != _headers.end()) {
    if (_toLower(teIt->second).find("chunked") != std::string::npos)
      _isChunked = true;
  }

  // Chunked takes precedence over Content-Length (RFC 2616 §4.4)
  if (_isChunked)
    _contentLength = 0;
}

// Decide the next state based on parsed headers.
void Request::_decideBodyState() {
  bool hasContentLength = (_headers.find("content-length") != _headers.end());

  if (_isChunked) {
    _state = CHUNKED_BODY;
  } else if (hasContentLength && _contentLength > 0) {
    _state = BODY;
  } else {
    _state = COMPLETE;
  }
}

// ============================================================
// Private: State Machine Steps
// ============================================================

// Step 1: Parse request line — "METHOD URI VERSION\r\n"
void Request::_parseRequestLine() {
  // Skip leading CRLFs (RFC 2616 §4.1)
  if (_skipLeadingCRLF())
    return;

  // Need a complete line
  size_t pos = _buffer.find("\r\n");
  if (pos == std::string::npos)
    return;

  // Protect against oversized request lines
  if (pos > 8192) {
    _setError(414);
    return;
  }

  // Extract, validate, and split the request line
  std::string line;
  if (!_extractLine(line))
    return;
  if (!_splitRequestLine(line))
    return;

  // Split URI into path + query
  _splitUri();

  // Validate path
  if (_path.empty() || _path[0] != '/') {
    _setError(400);
    return;
  }

  _state = HEADERS;
}

// Step 2: Parse headers — "Key: Value\r\n" lines, ended by "\r\n\r\n"
void Request::_parseHeaders() {
  // Extract the header block
  std::string headerBlock;
  if (!_extractHeaderBlock(headerBlock))
    return;

  // Protect against oversized headers
  if (headerBlock.size() > 16384) {
    _setError(431); // Request Header Fields Too Large
    return;
  }

  // Parse each header line
  size_t lineStart = 0;
  while (lineStart < headerBlock.size()) {
    size_t lineEnd = headerBlock.find("\r\n", lineStart);
    if (lineEnd == std::string::npos)
      lineEnd = headerBlock.size();

    std::string line = headerBlock.substr(lineStart, lineEnd - lineStart);
    lineStart = lineEnd + 2;

    if (line.empty())
      continue;
    if (!_parseHeaderLine(line))
      return;
  }

  // Post-parse validation
  _validateHeaders();
  if (_state == ERROR)
    return;

  // Decide next state based on body indicators
  _decideBodyState();
}

// Step 3: Parse body — read exactly _contentLength bytes
void Request::_parseBody() {
  if (_buffer.size() >= _contentLength) {
    _body = _buffer.substr(0, _contentLength);
    _buffer.erase(0, _contentLength);
    _state = COMPLETE;
  }
}

// Step 4: Parse chunked body — read chunks until size 0
void Request::_parseChunkedBody() {
  while (true) {
    // Find chunk size line
    size_t pos = _buffer.find("\r\n");
    if (pos == std::string::npos)
      return;

    // Parse hex chunk size (strip extensions like ";ext=val")
    std::string sizeStr = _buffer.substr(0, pos);
    size_t semiPos = sizeStr.find(';');
    if (semiPos != std::string::npos)
      sizeStr = sizeStr.substr(0, semiPos);

    long chunkSize;
    if (!_parseNumber(sizeStr, chunkSize, 16)) {
      _setError(400);
      return;
    }

    // Last chunk
    if (chunkSize == 0) {
      if (_buffer.size() < pos + 4) // "0\r\n\r\n"
        return;
      _buffer.erase(0, pos + 4);
      _state = COMPLETE;
      return;
    }

    // Wait for full chunk data + trailing \r\n
    size_t totalNeeded = pos + 2 + static_cast<size_t>(chunkSize) + 2;
    if (_buffer.size() < totalNeeded)
      return;

    // Extract chunk data and consume
    _body.append(_buffer, pos + 2, static_cast<size_t>(chunkSize));
    _buffer.erase(0, totalNeeded);
  }
}