#include "EnvBuilder.hpp"
#include "CGIHandler.hpp"
#include "../http/CGIResponseParser.hpp"

#include <iostream>
#include <map>
#include <string>
#include <vector>

static bool runHelloTest() {
    const std::string scriptPath = "./www/cgi-bin/hello.py";

    std::map<std::string, std::string> headers;
    headers["Host"] = "localhost:8080";
    headers["User-Agent"] = "webserv-cgi-test";
    headers["Content-Type"] = "text/plain";

    const std::string method = "GET";
    const std::string requestUri = "/cgi-bin/hello.py";
    const std::string queryString = "";
    const std::string serverName = "localhost";
    const std::string serverPort = "8080";
    const std::string remoteAddr = "127.0.0.1";
    const std::string scriptName = "/cgi-bin/hello.py";
    const std::string pathInfo = "";
    const std::string serverProtocol = "HTTP/1.1";
    const std::string contentLength = "0";
    const std::string contentType = "text/plain";

    EnvBuilder envBuilder;
    std::vector<std::string> env = envBuilder.buildFromParts(
        method,
        requestUri,
        queryString,
        headers,
        serverName,
        serverPort,
        remoteAddr,
        scriptName,
        pathInfo,
        serverProtocol,
        contentLength,
        contentType
    );

    CGIHandler handler;
    std::string output;
    std::string error;
    const std::string inputBody = "";
    const bool ok = handler.run(scriptPath, "", env, inputBody, output, error, 5);

    if (!ok) {
        std::cerr << "CGI hello failed: " << error << "\n";
        return false;
    }

    CGIResponseParser parser;
    std::map<std::string, std::string> parsedHeaders;
    std::string parsedBody;
    parser.parse(output, parsedHeaders, parsedBody);

    std::cout << "CGI headers:\n";
    for (std::map<std::string, std::string>::const_iterator it = parsedHeaders.begin();
         it != parsedHeaders.end(); ++it) {
        std::cout << it->first << ": " << it->second << "\n";
    }
    std::cout << "\nCGI body:\n" << parsedBody << "\n";
    return true;
}

static bool runEchoTest() {
    const std::string scriptPath = "./www/cgi-bin/echo.py";

    std::map<std::string, std::string> headers;
    headers["Host"] = "localhost:8080";
    headers["User-Agent"] = "webserv-cgi-test";
    headers["Content-Type"] = "text/plain";

    const std::string method = "POST";
    const std::string requestUri = "/cgi-bin/echo.py";
    const std::string queryString = "";
    const std::string serverName = "localhost";
    const std::string serverPort = "8080";
    const std::string remoteAddr = "127.0.0.1";
    const std::string scriptName = "/cgi-bin/echo.py";
    const std::string pathInfo = "";
    const std::string serverProtocol = "HTTP/1.1";
    const std::string inputBody = "ping";
    const std::string contentLength = "4";
    const std::string contentType = "text/plain";

    EnvBuilder envBuilder;
    std::vector<std::string> env = envBuilder.buildFromParts(
        method,
        requestUri,
        queryString,
        headers,
        serverName,
        serverPort,
        remoteAddr,
        scriptName,
        pathInfo,
        serverProtocol,
        contentLength,
        contentType
    );

    CGIHandler handler;
    std::string output;
    std::string error;
    const bool ok = handler.run(scriptPath, "", env, inputBody, output, error, 5);
    if (!ok) {
        std::cerr << "CGI echo failed: " << error << "\n";
        return false;
    }

    CGIResponseParser parser;
    std::map<std::string, std::string> parsedHeaders;
    std::string parsedBody;
    parser.parse(output, parsedHeaders, parsedBody);

    if (parsedBody.find("ping") == std::string::npos) {
        std::cerr << "CGI echo body mismatch\n";
        return false;
    }
    return true;
}

static bool runStatusTest() {
    const std::string scriptPath = "./www/cgi-bin/status.py";

    std::map<std::string, std::string> headers;
    headers["Host"] = "localhost:8080";
    headers["User-Agent"] = "webserv-cgi-test";
    headers["Content-Type"] = "text/plain";

    const std::string method = "GET";
    const std::string requestUri = "/cgi-bin/status.py";
    const std::string queryString = "";
    const std::string serverName = "localhost";
    const std::string serverPort = "8080";
    const std::string remoteAddr = "127.0.0.1";
    const std::string scriptName = "/cgi-bin/status.py";
    const std::string pathInfo = "";
    const std::string serverProtocol = "HTTP/1.1";
    const std::string contentLength = "0";
    const std::string contentType = "text/plain";

    EnvBuilder envBuilder;
    std::vector<std::string> env = envBuilder.buildFromParts(
        method,
        requestUri,
        queryString,
        headers,
        serverName,
        serverPort,
        remoteAddr,
        scriptName,
        pathInfo,
        serverProtocol,
        contentLength,
        contentType
    );

    CGIHandler handler;
    std::string output;
    std::string error;
    const std::string inputBody = "";
    const bool ok = handler.run(scriptPath, "", env, inputBody, output, error, 5);
    if (!ok) {
        std::cerr << "CGI status failed: " << error << "\n";
        return false;
    }

    CGIResponseParser parser;
    std::map<std::string, std::string> parsedHeaders;
    std::string parsedBody;
    parser.parse(output, parsedHeaders, parsedBody);

    std::map<std::string, std::string>::const_iterator it = parsedHeaders.find("Status");
    if (it == parsedHeaders.end() || it->second != "404 Not Found") {
        std::cerr << "CGI Status header missing or incorrect\n";
        return false;
    }
    return true;
}

int main() {
    const bool helloOk = runHelloTest();
    const bool echoOk = runEchoTest();
    const bool statusOk = runStatusTest();
    if (!helloOk || !echoOk || !statusOk) return 1;
    return 0;
}
