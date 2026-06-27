#include "EnvBuilder.hpp"
#include "CGIHandler.hpp"
#include "CGIResponseParser.hpp"

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <errno.h>

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

// Test startFromFile(): write body to a temp file, stream it to CGI stdin.
// This exercises the OOM-safe path that Person B's request parser uses.
static bool runFileBodyTest() {
    const std::string scriptPath = "./www/cgi-bin/echo.py";

    // --- Create a temp file containing the POST body ---
    char tmpl[] = "/tmp/webserv_test_XXXXXX";
    int tmpFd = mkstemp(tmpl);
    if (tmpFd < 0) {
        std::cerr << "runFileBodyTest: mkstemp failed\n";
        return false;
    }
    const char *bodyData = "file_body_payload";
    size_t bodyLen = std::strlen(bodyData);
    write(tmpFd, bodyData, bodyLen);
    close(tmpFd);

    // --- Build CGI environment ---
    std::map<std::string, std::string> headers;
    headers["Host"] = "localhost:8080";
    headers["User-Agent"] = "webserv-cgi-test";
    headers["Content-Type"] = "text/plain";

    std::ostringstream clOss;
    clOss << bodyLen;

    EnvBuilder envBuilder;
    std::vector<std::string> env = envBuilder.buildFromParts(
        "POST",
        "/cgi-bin/echo.py",
        "",
        headers,
        "localhost",
        "8080",
        "127.0.0.1",
        "/cgi-bin/echo.py",
        "",
        "HTTP/1.1",
        clOss.str(),
        "text/plain"
    );

    // --- Run CGI using the file-based API ---
    CGIHandler handler;
    if (!handler.startFromFile(scriptPath, "", env, std::string(tmpl), 5)) {
        std::cerr << "runFileBodyTest: startFromFile failed: " << handler.getError() << "\n";
        unlink(tmpl);
        return false;
    }

    // Drive the state machine with a local poll loop (same pattern as run()).
    while (handler.getState() != CGI_DONE && handler.getState() != CGI_ERROR) {
        if (handler.checkTimeout()) break;

        struct pollfd fds[3];
        int nfds = 0;
        int stdinIdx = -1, stdoutIdx = -1, stderrIdx = -1;

        if (handler.getStdinFd() >= 0) {
            stdinIdx = nfds;
            fds[nfds].fd = handler.getStdinFd();
            fds[nfds].events = POLLOUT;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (handler.getStdoutFd() >= 0) {
            stdoutIdx = nfds;
            fds[nfds].fd = handler.getStdoutFd();
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (handler.getStderrFd() >= 0) {
            stderrIdx = nfds;
            fds[nfds].fd = handler.getStderrFd();
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }

        if (nfds == 0) break;

        int pollRes = poll(fds, nfds, 100);
        if (pollRes < 0 && errno != EINTR) break;

        if (stdinIdx >= 0 && (fds[stdinIdx].revents & POLLOUT))
            handler.onStdinReady();
        if (stdoutIdx >= 0 && (fds[stdoutIdx].revents & (POLLIN | POLLHUP)))
            handler.onStdoutReady();
        if (stderrIdx >= 0 && (fds[stderrIdx].revents & (POLLIN | POLLHUP)))
            handler.onStderrReady();
    }

    unlink(tmpl);

    if (!handler.succeeded()) {
        std::cerr << "runFileBodyTest: CGI failed: " << handler.getError() << "\n";
        return false;
    }

    // --- Verify the CGI echoed the file body back ---
    CGIResponseParser parser;
    std::map<std::string, std::string> parsedHeaders;
    std::string parsedBody;
    parser.parse(handler.getOutput(), parsedHeaders, parsedBody);

    if (parsedBody.find("file_body_payload") == std::string::npos) {
        std::cerr << "runFileBodyTest: body mismatch, got: " << parsedBody << "\n";
        return false;
    }
    return true;
}

int main() {
    const bool helloOk = runHelloTest();
    const bool echoOk = runEchoTest();
    const bool statusOk = runStatusTest();
    const bool fileBodyOk = runFileBodyTest();

    if (!helloOk) std::cerr << "FAIL: hello\n";
    if (!echoOk) std::cerr << "FAIL: echo\n";
    if (!statusOk) std::cerr << "FAIL: status\n";
    if (!fileBodyOk) std::cerr << "FAIL: file-body\n";

    if (!helloOk || !echoOk || !statusOk || !fileBodyOk) return 1;
    std::cout << "All CGI tests passed.\n";
    return 0;
}
