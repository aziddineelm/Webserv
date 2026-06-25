#include "response.hpp"

// ============================================================
// Orthodox Canonical Form
// ============================================================

Response::Response()
	: _statusCode(200), _reasonPhrase("OK"),
	  _fileOffset(0), _fileSize(0),
	  _headersSent(false), _done(false) {}

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
// Does NOT read the file — Person A streams it via getNextChunk().
void Response::setFilePath(const std::string &path, size_t fileSize) {
	_filePath = path;
	_fileSize = fileSize;
	_fileOffset = 0;
	std::ostringstream oss;
	oss << fileSize;
	_headers["Content-Length"] = oss.str();
}

// ============================================================
// Convenience Builders
// ============================================================

// Build an error page response.
// If filePath is provided and the file is readable, uses that file as the body.
// Otherwise, falls back to a generated default HTML page.
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

// Build a redirect response (301 / 302)
void Response::buildRedirect(int code, const std::string &location) {
	setStatus(code);
	setHeader("Location", location);
	setBody("");
}

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

// Returns the next chunk of the response.
// Call 1: returns headers. Call 2+: returns body chunks.
// When isDone() is true, stop calling.
std::string Response::getNextChunk() {
	if (_done)
		return "";

	// First call: return headers only
	if (!_headersSent) {
		_headersSent = true;
		// If string mode with no body, mark done immediately
		if (_filePath.empty() && _body.empty())
			_done = true;
		return getHeaders();
	}

	// String mode: body is sent in a single second call
	if (_filePath.empty()) {
		_done = true;
		return _body;
	}

	// File mode: read 8KB from disk using open/seekg/close (no ifstream member)
	std::ifstream file(_filePath.c_str(), std::ios::binary);
	if (!file.is_open()) {
		_done = true;
		return "";
	}
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

// ============================================================
// Legacy Serialize — still works for string-mode responses
// ============================================================

// Output format (RFC 2616 §6):
//   HTTP/1.1 200 OK\r\n
//   Content-Type: text/html\r\n
//   Content-Length: 45\r\n
//   \r\n
//   <html>...</html>
std::string Response::serialize() const {
	return getHeaders() + _body;
}

// ============================================================
// Getters
// ============================================================

int Response::getStatusCode() const { return _statusCode; }

std::string Response::getHeader(const std::string &key) const {
	std::map<std::string, std::string>::const_iterator it = _headers.find(key);
	if (it != _headers.end())
		return it->second;
	return "";
}

const std::string &Response::getBody() const { return _body; }

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
		case 503: return "Service Unavailable";
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
		<< "        *{margin:0;padding:0;box-sizing:border-box}\n"
		<< "        body{display:flex;align-items:center;justify-content:center;"
		<< "min-height:100vh;background:#0a0a0a;color:#e0e0e0;"
		<< "font-family:'Courier New',Courier,monospace;overflow:hidden}\n"
		<< "        .container{text-align:center;padding:2rem}\n"
		<< "        .code{font-size:clamp(5rem,15vw,10rem);font-weight:700;"
		<< "letter-spacing:-4px;color:#fff;line-height:1;position:relative}\n"
		<< "        .code::after{content:'';display:block;width:60px;height:2px;"
		<< "background:#333;margin:1.5rem auto}\n"
		<< "        .message{font-size:clamp(0.85rem,2vw,1.1rem);color:#989494;"
		<< "letter-spacing:2px;text-transform:uppercase}\n"
		<< "        .footer{position:fixed;bottom:1.5rem;left:0;width:100%;"
		<< "text-align:center;font-size:0.7rem;color:#2a2a2a;letter-spacing:1px}\n"
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

// Read an entire file into a string.
// Returns true on success, false if the file doesn't exist or can't be read.
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
