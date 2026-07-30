#include "CGIHandler.hpp"
#include "ProcessSpawner.hpp"
#include "CGIResponseParser.hpp"

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sstream>
#include "EnvBuilder.hpp"
#include "../http/request/request.hpp"
#include "../config/ServerConfig.hpp"
namespace {
    const size_t MAX_PATH_LEN = 4096;
    const size_t READ_BUF_SIZE = 4096;
    const size_t BODY_BUF_SIZE = 8192;
    const size_t MAX_HEADERS_SIZE = 65536;
}

// --- Construction / Destruction ---

CGIHandler::CGIHandler()
    : _state(CGI_IDLE), _pid(-1),
      _stdinFd(-1), _stdoutFd(-1), _stderrFd(-1),
      _inputPos(0), _bodyFileFd(-1),
      _startTime(0),
      _exitStatus(0), _exited(false),
      _headersParsed(false),
      _outputQueueOffset(0),
      _lastActivityTime(0), _idleTimeoutSeconds(30), _maxTimeoutSeconds(0) {}

CGIHandler::~CGIHandler() {
    cleanup();
}

// --- Helper: close FD ---

void CGIHandler::closeFd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

// --- Helper: int to string (no C++11) ---

std::string CGIHandler::_intToStr(int n) {
    std::ostringstream oss;
    oss << n;
    return oss.str();
}

// --- Non-blocking API ---

bool CGIHandler::start(const std::string& scriptPath,
                       const std::string& interpreterPath,
                       const std::vector<std::string>& env,
                       const std::string& bodyFilePath,
                       int idleTimeoutSec,
                       int maxTimeoutSec) {
    cleanup();

    _input.clear();
    _inputPos = 0;
    _bodyFileFd = -1;
    _error.clear();
    _exitStatus = 0;
    _exited = false;
    _idleTimeoutSeconds = idleTimeoutSec;
    _maxTimeoutSeconds = maxTimeoutSec;
    _headersParsed = false;
    _rawBuffer.clear();
    _cgiHeaders.clear();
    _outputQueue.clear();
    _outputQueueOffset = 0;

    // Open the body file for streaming if a path was provided.
    if (!bodyFilePath.empty()) {
        struct stat st;
        if (stat(bodyFilePath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            _error = "CGIHandler::start: body file not found";
            _state = CGI_ERROR;
            return false;
        }
        _bodyFileFd = open(bodyFilePath.c_str(), O_RDONLY);
        if (_bodyFileFd < 0) {
            _error = "CGIHandler::start: failed to open body file";
            _state = CGI_ERROR;
            return false;
        }
    }

    std::vector<std::string> argv;
    if (!interpreterPath.empty()) {
        argv.push_back(interpreterPath);
    }
    argv.push_back(scriptPath);

    ProcessSpawner spawner;
    _pid = spawner.spawn(argv, env, _stdinFd, _stdoutFd, _stderrFd);
    if (_pid < 0) {
        closeFd(_bodyFileFd);
        _error = "CGIHandler::start failed to spawn process";
        _state = CGI_ERROR;
        return false;
    }

    // Set all pipe FDs to non-blocking.
    fcntl(_stdinFd, F_SETFL, O_NONBLOCK);
    fcntl(_stdoutFd, F_SETFL, O_NONBLOCK);
    fcntl(_stderrFd, F_SETFL, O_NONBLOCK);

    _startTime = time(NULL);
    _lastActivityTime = _startTime;

    // If there is no body file, close stdin immediately.
    if (_bodyFileFd < 0) {
        closeFd(_stdinFd);
        _state = CGI_READING;
    } else {
        _state = CGI_WRITING;
    }

    return true;
}

bool CGIHandler::startFromRequest(const Request& req,
                                  const ServerConfig& config,
                                  const std::string& scriptPath,
                                  const std::string& interpreterPath,
                                  const std::string& clientIp)
{
    const LocationConfig* loc = config.matchLocation(req.getUri());
    int idleTimeoutSec = loc ? loc->cgi_idle_timeout : 30;
    int maxTimeoutSec  = loc ? loc->cgi_max_timeout  : 0;

    EnvBuilder envBuilder;
    std::vector<std::string> envVars = envBuilder.buildEnv(req, config, clientIp);

    std::string resolvedScript = scriptPath;
    char resolvedBuf[MAX_PATH_LEN];
    if (realpath(resolvedScript.c_str(), resolvedBuf) != NULL) {
        resolvedScript = resolvedBuf;
    }

    // Ensure SCRIPT_FILENAME is set for php-cgi
    bool hasScriptFilename = false;
    for (size_t i = 0; i < envVars.size(); ++i) {
        if (envVars[i].compare(0, 16, "SCRIPT_FILENAME=") == 0) {
            envVars[i] = "SCRIPT_FILENAME=" + resolvedScript;
            hasScriptFilename = true;
            break;
        }
    }
    if (!hasScriptFilename) {
        envVars.push_back("SCRIPT_FILENAME=" + resolvedScript);
    }

    return start(resolvedScript, interpreterPath, envVars, req.getBodyFilePath(), idleTimeoutSec, maxTimeoutSec);
}

void CGIHandler::onStdinReady() {
    if (_stdinFd < 0 || _state == CGI_DONE || _state == CGI_ERROR) return;

    // If the in-memory buffer is exhausted, try to refill from the body file.
    if (_bodyFileFd >= 0 && _inputPos >= _input.size()) {
        char buf[BODY_BUF_SIZE];
        ssize_t bytesRead = read(_bodyFileFd, buf, sizeof(buf));
        if (bytesRead > 0) {
            _input.assign(buf, static_cast<size_t>(bytesRead));
            _inputPos = 0;
        } else {
            // EOF or read error — done reading from the body file.
            closeFd(_bodyFileFd);
        }
    }

    // Write current chunk to CGI's stdin pipe.
    if (_inputPos < _input.size()) {
        ssize_t n = write(_stdinFd, _input.data() + _inputPos, _input.size() - _inputPos);
        if (n > 0) {
            _inputPos += static_cast<size_t>(n);
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            closeFd(_stdinFd);
            closeFd(_bodyFileFd);
            _state = CGI_READING;
            return;
        }
    }

    // All data written: both the in-memory buffer is empty and the file is done.
    if (_inputPos >= _input.size() && _bodyFileFd < 0) {
        closeFd(_stdinFd);
        _state = CGI_READING;
    }
}

void CGIHandler::onStdoutReady() {
    if (_stdoutFd < 0 || _state == CGI_DONE || _state == CGI_ERROR) return;

    char buf[READ_BUF_SIZE];
    ssize_t n = read(_stdoutFd, buf, sizeof(buf));
    if (n > 0) {
        // Update inactivity timer on every successful read
        _lastActivityTime = time(NULL);

        if (!_headersParsed) {
            // Phase 1: Accumulating data until we find header/body separator
            _rawBuffer.append(buf, static_cast<size_t>(n));

            // Safety: reject CGI output with absurdly large headers (MAX_HEADERS_SIZE limit)
            if (_rawBuffer.size() > MAX_HEADERS_SIZE) {
                _error = "CGIHandler: CGI headers exceeded " + std::to_string(MAX_HEADERS_SIZE) + " bytes limit";
                _state = CGI_ERROR;
                closeFd(_stdoutFd);
                return;
            }

            _tryParseHeaders();
        } else {
            // Phase 2: Headers parsed — body bytes go straight to output queue
            _outputQueue.append(buf, static_cast<size_t>(n));
        }
    } else if (n == 0) {
        // EOF — CGI closed stdout.
        if (!_headersParsed) {
            // Script closed stdout before producing headers — treat entire buffer as body
            _headersParsed = true;
            _outputQueue.append(_rawBuffer);
            _rawBuffer.clear();
        }
        closeFd(_stdoutFd);
        checkDone();
    } else if (errno != EAGAIN && errno != EINTR) {
        closeFd(_stdoutFd);
        checkDone();
    }
}

void CGIHandler::onStderrReady() {
    if (_stderrFd < 0 || _state == CGI_DONE || _state == CGI_ERROR) return;

    char buf[READ_BUF_SIZE];
    ssize_t n = read(_stderrFd, buf, sizeof(buf));
    if (n > 0) {
        _error.append(buf, static_cast<size_t>(n));
    } else if (n == 0) {
        closeFd(_stderrFd);
        checkDone();
    } else if (errno != EAGAIN && errno != EINTR) {
        closeFd(_stderrFd);
        checkDone();
    }
}

// --- Private: Incremental header parsing ---

void CGIHandler::_tryParseHeaders() {
    // Check if separator exists in buffer
    size_t sepPos = _rawBuffer.find("\r\n\r\n");
    if (sepPos == std::string::npos) {
        sepPos = _rawBuffer.find("\n\n");
    }
    if (sepPos == std::string::npos)
        return; // Not enough data yet

    // Reuse the existing CGIResponseParser for correct header parsing
    // (handles case-insensitive Status, trimStatusValue, etc.)
    CGIResponseParser parser;
    std::string parsedBody;
    parser.parse(_rawBuffer, _cgiHeaders, parsedBody);

    // Push any body bytes that arrived with the headers
    _outputQueue.append(parsedBody);
    _rawBuffer.clear();
    _headersParsed = true;
    _state = CGI_HEADERS_READY;
}

// --- Private: Output queue compaction ---

void CGIHandler::_compactOutputQueue() {
    if (_outputQueueOffset > 0 && _outputQueueOffset > _outputQueue.size() / 2) {
        _outputQueue.erase(0, _outputQueueOffset);
        _outputQueueOffset = 0;
    }
}

// --- Dual Timeout ---

bool CGIHandler::checkTimeout() {
    if (_state == CGI_DONE || _state == CGI_ERROR || _state == CGI_IDLE) return false;

    time_t now = time(NULL);

    // Check 1: Inactivity timeout (nginx-style proxy_read_timeout)
    // Fires when no data has been received for _idleTimeoutSeconds
    bool timedOut = false;
    if (_idleTimeoutSeconds > 0 && (now - _lastActivityTime) >= _idleTimeoutSeconds) {
        _error = "CGIHandler: process timed out (no activity for "
                 + _intToStr(_idleTimeoutSeconds) + "s)";
        timedOut = true;
    }

    // Check 2: Absolute timeout (php-fpm-style request_terminate_timeout)
    // Fires when total wall-clock time exceeds _maxTimeoutSeconds
    if (!timedOut && _maxTimeoutSeconds > 0 && (now - _startTime) >= _maxTimeoutSeconds) {
        _error = "CGIHandler: process exceeded absolute timeout ("
                 + _intToStr(_maxTimeoutSeconds) + "s)";
        timedOut = true;
    }

    if (timedOut) {
        if (_pid > 0 && !_exited) {
            kill(_pid, SIGKILL);
            waitpid(_pid, &_exitStatus, 0);
            _exited = true;
        }
        closeFd(_stdinFd);
        closeFd(_stdoutFd);
        closeFd(_stderrFd);
        _state = CGI_ERROR;
        return true;
    }
    return false;
}

void CGIHandler::tryReap() {
    if (_pid > 0 && !_exited) {
        pid_t res = waitpid(_pid, &_exitStatus, WNOHANG);
        if (res == _pid) _exited = true;
    }
}

void CGIHandler::checkDone() {
    // Both stdout and stderr must be closed before we consider CGI done.
    if (_stdoutFd >= 0 || _stderrFd >= 0) return;

    // Try to reap the child process.
    tryReap();
    if (!_exited) {
        // Child hasn't exited yet — do a blocking wait since pipes are closed.
        waitpid(_pid, &_exitStatus, 0);
        _exited = true;
    }

    _state = CGI_DONE;
}

void CGIHandler::cleanup() {
    if (_pid > 0 && !_exited) {
        kill(_pid, SIGKILL);
        waitpid(_pid, &_exitStatus, 0);
        _exited = true;
    }
    closeFd(_stdinFd);
    closeFd(_stdoutFd);
    closeFd(_stderrFd);
    closeFd(_bodyFileFd);
    _pid = -1;
    _state = CGI_IDLE;
}

// --- Getters ---

int CGIHandler::getStdinFd() const { return _stdinFd; }
int CGIHandler::getStdoutFd() const { return _stdoutFd; }
int CGIHandler::getStderrFd() const { return _stderrFd; }
CgiState CGIHandler::getState() const { return _state; }
const std::string& CGIHandler::getError() const { return _error; }

bool CGIHandler::succeeded() const {
    return _state == CGI_DONE && _exited
           && WIFEXITED(_exitStatus) && WEXITSTATUS(_exitStatus) == 0;
}

// --- Streaming API ---

bool CGIHandler::headersReady() const {
    return _headersParsed;
}

const std::map<std::string, std::string>& CGIHandler::getCgiHeaders() const {
    return _cgiHeaders;
}

bool CGIHandler::hasPendingOutput() const {
    return _outputQueueOffset < _outputQueue.size();
}

std::string CGIHandler::popOutput(size_t maxBytes) {
    size_t available = _outputQueue.size() - _outputQueueOffset;
    if (available == 0)
        return "";

    size_t chunkSize = (maxBytes < available) ? maxBytes : available;
    std::string chunk = _outputQueue.substr(_outputQueueOffset, chunkSize);
    _outputQueueOffset += chunkSize;

    // Periodically reclaim memory
    _compactOutputQueue();

    return chunk;
}

bool CGIHandler::outputFullyConsumed() const {
    return _stdoutFd < 0 && _outputQueueOffset >= _outputQueue.size();
}

