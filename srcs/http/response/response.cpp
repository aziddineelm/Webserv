#include "response.hpp"
#include <sstream>
#include <fstream>
#include <cstdlib>

// ============================================================
// Orthodox Canonical Form
// ============================================================

Response::Response()
	: _statusCode(200), _reasonPhrase("OK"),
	  _fileOffset(0), _fileSize(0),
	  _headersSent(false), _done(false), _isChunked(false), _cgiEOF(false) {}

Response::Response(const Response &other) { *this = other; }

Response &Response::operator=(const Response &other) {
	if (this != &other) {
		_statusCode = other._statusCode;
		_reasonPhrase = other._reasonPhrase;
		_headers = other._headers;
		_body = other._body;
		_filePath = other._filePath;
		_fileOffset = other._fileOffset;
		_fileSize = other._fileSize;
		_headersSent = other._headersSent;
		_done = other._done;
		_isChunked = other._isChunked;
		_cgiEOF = other._cgiEOF;
		_cgiScriptPath = other._cgiScriptPath;
		_cgiInterpreterPath = other._cgiInterpreterPath;
	}
	return *this;
}

Response::~Response() {}

// ============================================================
// Builder Methods
// ============================================================

void Response::setStatus(int code) {
	_statusCode = code;
	_reasonPhrase = getReasonPhrase(code);
}

int Response::getStatusCode() const { return _statusCode; }

void Response::setHeader(const std::string &key, const std::string &value) {
	_headers[key] = value;
}

// setBody() auto-sets Content-Length (DRY — never set it manually)
void Response::setBody(const std::string &body) {
	_body = body;
	std::ostringstream oss;
	oss << _body.size();
	_headers["Content-Length"] = oss.str();
}

// Set file-mode: store path + size, auto-set Content-Length.
void Response::setFilePath(const std::string &path, size_t fileSize) {
	_filePath = path;
	_fileSize = fileSize;
	_fileOffset = 0;
	std::ostringstream oss;
	oss << fileSize;
	_headers["Content-Length"] = oss.str();
}

// ============================================================
// CGI Metadata — Router tells EventLoop what to execute
// ============================================================

void Response::setCgiScript(const std::string &script, const std::string &interpreter) {
	_cgiScriptPath = script;
	_cgiInterpreterPath = interpreter;
}

std::string Response::getCgiScript() const { return _cgiScriptPath; }
std::string Response::getCgiInterpreter() const { return _cgiInterpreterPath; }
bool Response::isCgi() const { return !_cgiScriptPath.empty(); }

// ============================================================
// Convenience Builders
// ============================================================

void Response::buildErrorPage(int code, const std::string &filePath) {
	setStatus(code);
	setHeader("Content-Type", "text/html");

	std::string html;
	if (!filePath.empty() && _readFile(filePath, html)) {
		setBody(html);
	} else {
		setBody(_generateErrorHtml(code, _reasonPhrase));
	}
}

void Response::buildRedirect(int code, const std::string &location) {
	setStatus(code);
	setHeader("Location", location);
	setBody("");
}

void Response::buildFromCgiHeaders(const std::map<std::string, std::string> &cgiHeaders) {
	// Extract status code from CGI headers (default 200)
	int statusCode = 200;
	std::map<std::string, std::string>::const_iterator sit = cgiHeaders.find("Status");
	if (sit != cgiHeaders.end()) {
		statusCode = std::atoi(sit->second.c_str());
		if (statusCode <= 0)
			statusCode = 200;
	}
	setStatus(statusCode);

	// Copy CGI headers into the HTTP response (skip "Status" — it's not an HTTP header)
	for (std::map<std::string, std::string>::const_iterator hi = cgiHeaders.begin(); hi != cgiHeaders.end(); ++hi) {
		if (hi->first != "Status")
			setHeader(hi->first, hi->second);
	}

	// Chunked mode: no Content-Length, stream body live
	_headers.erase("Content-Length");
	setHeader("Transfer-Encoding", "chunked");
	_isChunked = true;
}

void Response::markDone() { _cgiEOF = true; }

// ============================================================
// Streaming API — Person A calls getNextChunk() in a loop
// ============================================================

// Build the raw HTTP headers string (status line + headers + blank line)
std::string Response::getHeaders() const {
	std::ostringstream oss;

	// Status line
	oss << "HTTP/1.1 " << _statusCode << " " << _reasonPhrase << "\r\n";

	// Headers
	std::map<std::string, std::string>::const_iterator it;
	for (it = _headers.begin(); it != _headers.end(); ++it)
		oss << it->first << ": " << it->second << "\r\n";

	// Blank line separating headers from body
	oss << "\r\n";

	return oss.str();
}

std::string Response::getNextChunk() {
	if (_done)
		return "";

	// First call: return headers only
	if (!_headersSent) {
		_headersSent = true;
		// If string mode with no body and not chunked, mark done immediately
		if (_filePath.empty() && _body.empty() && !_isChunked)
			_done = true;
		return getHeaders();
	}

	// ── State: CGI Chunked Mode (Live Streaming) ──
	if (_isChunked) {
		if (_cgiEOF) {
			_done = true;
			return "0\r\n\r\n";
		}
		return "";
	}

	// ── State: String Mode (HTML Errors / Redirects) ──
	if (_filePath.empty()) {
		_done = true;
		return _body;
	}

	// ── State: File Mode (Static File Streaming) ──
	std::ifstream file(_filePath.c_str(), std::ios::binary);
	if (!file.is_open()) {
		_done = true;
		return "";
	}
	// ── Clarification: seekg is used to resume reading exactly where the last chunk left off, keeping memory usage at 8KB
	file.seekg(static_cast<std::streamoff>(_fileOffset));

	char buffer[8192];
	file.read(buffer, sizeof(buffer));
	size_t bytesRead = static_cast<size_t>(file.gcount());
	_fileOffset += bytesRead;

	if (_fileOffset >= _fileSize || bytesRead == 0)
		_done = true;

	return std::string(buffer, bytesRead);
}

bool Response::isDone() const { return _done; }

std::string Response::formatChunk(const std::string &data) {
	if (data.empty())
		return "";
	std::ostringstream oss;
	oss << std::hex << data.size() << "\r\n";
	oss << data << "\r\n";
	return oss.str();
}



// ============================================================
// Static Utility — Status Code Table
// ============================================================

std::string Response::getReasonPhrase(int code) {
	switch (code) {
		// 2xx Success
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		// 3xx Redirection
		case 301: return "Moved Permanently";
		case 302: return "Found";
		// 4xx Client Error
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 431: return "Request Header Fields Too Large";
		// 5xx Server Error
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		// 4xx (additional)
		case 408: return "Request Timeout";
		default:  return "Unknown";
	}
}

// ============================================================
// Static Utility — MIME Type Table
// ============================================================

std::string Response::getMimeType(const std::string &extension) {
		// Text
		if (extension == ".html" || extension == ".htm") return "text/html";
		if (extension == ".css")  return "text/css";
		if (extension == ".txt")  return "text/plain";
		if (extension == ".xml")  return "application/xml";
		// Application
		if (extension == ".js")   return "application/javascript";
		if (extension == ".json") return "application/json";
		if (extension == ".pdf")  return "application/pdf";
		// Image
		if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
		if (extension == ".png")  return "image/png";
		if (extension == ".gif")  return "image/gif";
		if (extension == ".ico")  return "image/x-icon";
		if (extension == ".svg")  return "image/svg+xml";
		// Video/Audio
		if (extension == ".mp4")  return "video/mp4";
		if (extension == ".mp3")  return "audio/mpeg";
		if (extension == ".webm") return "video/webm";
		// Archive
		if (extension == ".zip")  return "application/zip";
		if (extension == ".gz")   return "application/gzip";
		// Default — unknown binary
		return "application/octet-stream";
}

// ============================================================
// Private — Default Error Page HTML Generator (fallback)
// ============================================================

std::string Response::_generateErrorHtml(int code, const std::string &reason) {
	std::ostringstream oss;
	oss << "<!DOCTYPE html>\n"
		<< "<html lang=\"en\">\n"
		<< "<head>\n"
		<< "    <meta charset=\"UTF-8\">\n"
		<< "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
		<< "    <title>" << code << " — webserv</title>\n"
		<< "    <link rel=\"icon\" type=\"image/png\" href=\"/imgs/icon.png\">\n"
		<< "    <style>\n"
		<< "        * {\n"
		<< "            margin: 0;\n"
		<< "            padding: 0;\n"
		<< "            box-sizing: border-box;\n"
		<< "        }\n"
		<< "        body {\n"
		<< "            display: flex;\n"
		<< "            align-items: center;\n"
		<< "            justify-content: center;\n"
		<< "            min-height: 100vh;\n"
		<< "            background: #0a0a0a;\n"
		<< "            color: #e0e0e0;\n"
		<< "            font-family: 'Courier New', Courier, monospace;\n"
		<< "            overflow: hidden;\n"
		<< "        }\n"
		<< "        .container {\n"
		<< "            text-align: center;\n"
		<< "            padding: 2rem;\n"
		<< "        }\n"
		<< "        .code {\n"
		<< "            font-size: clamp(5rem, 15vw, 10rem);\n"
		<< "            font-weight: 700;\n"
		<< "            letter-spacing: -4px;\n"
		<< "            color: #fff;\n"
		<< "            line-height: 1;\n"
		<< "            position: relative;\n"
		<< "        }\n"
		<< "        .code::after {\n"
		<< "            content: '';\n"
		<< "            display: block;\n"
		<< "            width: 60px;\n"
		<< "            height: 2px;\n"
		<< "            background: #333;\n"
		<< "            margin: 1.5rem auto;\n"
		<< "        }\n"
		<< "        .message {\n"
		<< "            font-size: clamp(0.85rem, 2vw, 1.1rem);\n"
		<< "            color: #989494;\n"
		<< "            letter-spacing: 2px;\n"
		<< "            text-transform: uppercase;\n"
		<< "        }\n"
		<< "        .footer {\n"
		<< "            position: fixed;\n"
		<< "            bottom: 1.5rem;\n"
		<< "            left: 0;\n"
		<< "            width: 100%;\n"
		<< "            text-align: center;\n"
		<< "            font-size: 0.7rem;\n"
		<< "            color: #2a2a2a;\n"
		<< "            letter-spacing: 1px;\n"
		<< "        }\n"
		<< "    </style>\n"
		<< "</head>\n"
		<< "<body>\n"
		<< "    <div class=\"container\">\n"
		<< "        <div class=\"code\">" << code << "</div>\n"
		<< "        <div class=\"message\">" << reason << "</div>\n"
		<< "    </div>\n"
		<< "    <div class=\"footer\">webserv</div>\n"
		<< "</body>\n"
		<< "</html>\n";
	return oss.str();
}

bool Response::_readFile(const std::string &path, std::string &content) {
	std::ifstream file(path.c_str(), std::ios::binary);
	if (!file.is_open())
		return false;

	std::ostringstream oss;
	oss << file.rdbuf();
	if (file.fail())
		return false;

	content = oss.str();
	return true;
}
