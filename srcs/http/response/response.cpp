#include "response.hpp"

Response::Response() : _statusCode(200), _reasonPhrase("OK") {}

Response::Response(const Response &other) { *this = other; }

Response &Response::operator=(const Response &other) {
	if (this != &other) {
		_statusCode = other._statusCode;
		_reasonPhrase = other._reasonPhrase;
		_headers = other._headers;
		_body = other._body;
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
// Serialize — Response → raw HTTP bytes
// ============================================================

// Output format (RFC 2616 §6):
//   HTTP/1.1 200 OK\r\n
//   Content-Type: text/html\r\n
//   Content-Length: 45\r\n
//   \r\n
//   <html>...</html>
std::string Response::serialize() const {
	std::ostringstream oss;

	// Status line
	oss << "HTTP/1.1 " << _statusCode << " " << _reasonPhrase << "\r\n";

	// Headers
	std::map<std::string, std::string>::const_iterator it;
	for (it = _headers.begin(); it != _headers.end(); ++it)
		oss << it->first << ": " << it->second << "\r\n";

	// Blank line separating headers from body
	oss << "\r\n";

	// Body
	oss << _body;

	return oss.str();
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
		// Default — unknown binary
		return "application/octet-stream";
}

// ============================================================
// Private — Default Error Page HTML Generator (fallback)
// ============================================================

std::string Response::_generateErrorHtml(int code, const std::string &reason) {
	std::ostringstream oss;
	oss << "<!DOCTYPE html>\n"
			<< "<html>\n"
			<< "<head>\n"
			<< "    <title>webserv</title>\n"
			<< "    <link rel=\"icon\" type=\"image/png\" href=\"/imgs/icon.png\">\n"
			<< "    <style>\n"
			<< "        @import url('https://fonts.googleapis.com/css?family=Fira+Mono:400');\n"
			<< "\n"
			<< "        body{\n"
			<< "            display: flex;\n"
			<< "            width: 100vw;\n"
			<< "            height: 100vh;\n"
			<< "            align-items: center;\n"
			<< "            justify-content: center;\n"
			<< "            margin: 0;\n"
			<< "            background: #131313;\n"
			<< "            color: #fff;\n"
			<< "            font-size: 96px;\n"
			<< "            font-family: 'Fira Mono', monospace;\n"
			<< "            letter-spacing: -7px;\n"
			<< "        }\n"
			<< "\n"
			<< "        p {\n"
			<< "            font-size: 24px;\n"
			<< "            letter-spacing: -1px;\n"
			<< "        }\n"
			<< "\n"
			<< "        footer {\n"
			<< "            position: absolute;\n"
			<< "            bottom: 0;\n"
			<< "            width: 100%;\n"
			<< "            height: 30px;\n"
			<< "            background-color: #131313;\n"
			<< "            color: #fff;\n"
			<< "            font-size: 12px;\n"
			<< "            font-family: 'Fira Mono', monospace;\n"
			<< "            letter-spacing: -1px;\n"
			<< "            text-align: center;\n"
			<< "        }\n"
			<< "\n"
			<< "        div{\n"
			<< "            animation: glitch 1s linear infinite;\n"
			<< "        }\n"
			<< "\n"
			<< "        @keyframes glitch{\n"
			<< "            2%,64%{\n"
			<< "                transform: translate(2px,0) skew(0deg);\n"
			<< "            }\n"
			<< "            4%,60%{\n"
			<< "                transform: translate(-2px,0) skew(0deg);\n"
			<< "            }\n"
			<< "            62%{\n"
			<< "                transform: translate(0,0) skew(5deg);\n"
			<< "            }\n"
			<< "        }\n"
			<< "\n"
			<< "        div:before,\n"
			<< "        div:after{\n"
			<< "            content: attr(title);\n"
			<< "            position: absolute;\n"
			<< "            left: 0;\n"
			<< "        }\n"
			<< "\n"
			<< "        div:before{\n"
			<< "            animation: glitchTop 1s linear infinite;\n"
			<< "            clip-path: polygon(0 0, 100% 0, 100% 33%, 0 33%);\n"
			<< "            -webkit-clip-path: polygon(0 0, 100% 0, 100% 33%, 0 33%);\n"
			<< "        }\n"
			<< "\n"
			<< "        @keyframes glitchTop{\n"
			<< "            2%,64%{\n"
			<< "                transform: translate(2px,-2px);\n"
			<< "            }\n"
			<< "            4%,60%{\n"
			<< "                transform: translate(-2px,2px);\n"
			<< "            }\n"
			<< "            62%{\n"
			<< "                transform: translate(13px,-1px) skew(-13deg);\n"
			<< "            }\n"
			<< "        }\n"
			<< "\n"
			<< "        div:after{\n"
			<< "            animation: glitchBotom 1.5s linear infinite;\n"
			<< "            clip-path: polygon(0 67%, 100% 67%, 100% 100%, 0 100%);\n"
			<< "            -webkit-clip-path: polygon(0 67%, 100% 67%, 100% 100%, 0 100%);\n"
			<< "        }\n"
			<< "\n"
			<< "        @keyframes glitchBotom{\n"
			<< "            2%,64%{\n"
			<< "                transform: translate(-2px,0);\n"
			<< "            }\n"
			<< "            4%,60%{\n"
			<< "                transform: translate(-2px,0);\n"
			<< "            }\n"
			<< "            62%{\n"
			<< "                transform: translate(-22px,5px) skew(21deg);\n"
			<< "            }\n"
			<< "        }\n"
			<< "    </style>\n"
			<< "</head>\n"
			<< "<body>\n"
			<< "<div title=\"" << code << "\">" << code << "</div>\n"
			<< "<span> | </span>\n"
			<< "<p>\n"
			<< "    " << reason << "\n"
			<< "</p>\n"
			<< "</body>\n"
			<< "<footer>\n"
			<< "    webserv v1.0\n"
			<< "</footer>\n"
			<< "</html>\n";
	return oss.str();
}

// Read an entire file into a string.
// Returns true on success, false if the file doesn't exist or can't be read.
bool Response::_readFile(const std::string &path, std::string &content) {
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return false;

	std::ostringstream oss;
	oss << file.rdbuf();
	if (file.fail())
		return false;

	content = oss.str();
	return true;
}
