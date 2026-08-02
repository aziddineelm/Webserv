#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include <vector>
#include <map>
#include <time.h>

class Request;
class ServerConfig;

enum CgiState {
    CGI_IDLE,
    CGI_WRITING,
    CGI_READING,
    CGI_HEADERS_READY,
    CGI_DONE,
    CGI_ERROR
};


// --- Non-blocking API (for main event loop integration) ---
class CGIHandler {
public:
    CGIHandler();
    ~CGIHandler();

    bool start(const std::string& scriptPath,
               const std::string& interpreterPath,
               const std::vector<std::string>& env,
               const std::string& bodyFilePath,
               int idleTimeoutSec = 30,
               int maxTimeoutSec = 0);

    bool startFromRequest(const Request& req,
                          const ServerConfig& config,
                          const std::string& scriptPath,
                          const std::string& interpreterPath,
                          const std::string& clientIp = "");

    void onStdinReady();
    void onStdoutReady();
    void onStderrReady();

    bool checkTimeout();

    // Kill the process if still running, close any open FDs, reap child.
    void cleanup();

    // --- Getters ---
    int getStdinFd() const;
    int getStdoutFd() const;
    int getStderrFd() const;

    CgiState getState() const;
    const std::string& getError() const;

    bool succeeded() const;

    // --- Streaming API ---
    bool headersReady() const;
    const std::map<std::string, std::string>& getCgiHeaders() const;
    bool hasPendingOutput() const;
    std::string popOutput(size_t maxBytes = 8192);
    bool outputFullyConsumed() const;


private:
    CgiState _state;
    int _pid;
    int _stdinFd;
    int _stdoutFd;
    int _stderrFd;

    std::string _input;
    size_t _inputPos;
    std::string _error;

    int _bodyFileFd;

    time_t _startTime;
    int _exitStatus;
    bool _exited;

    // --- Streaming state ---
    bool _headersParsed;
    std::string _rawBuffer;
    std::map<std::string, std::string> _cgiHeaders;
    std::string _outputQueue;
    size_t _outputQueueOffset;

    // --- Dual timeout state ---
    time_t _lastActivityTime;
    int _idleTimeoutSeconds;
    int _maxTimeoutSeconds;

    // --- Private helpers ---
    void tryReap();
    void checkDone();
    void closeFd(int& fd);
    void _tryParseHeaders();
    void _compactOutputQueue();
    static std::string _intToStr(int n);
};

#endif
