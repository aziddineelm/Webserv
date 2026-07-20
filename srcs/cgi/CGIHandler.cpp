#include "CGIHandler.hpp"
#include "ProcessSpawner.hpp"

#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "EnvBuilder.hpp"
#include "../http/request/request.hpp"
#include "../config/ServerConfig.hpp"

// --- Construction / Destruction ---

CGIHandler::CGIHandler()
    : _state(CGI_IDLE), _pid(-1),
      _stdinFd(-1), _stdoutFd(-1), _stderrFd(-1),
      _inputPos(0), _bodyFileFd(-1),
      _startTime(0), _timeoutSeconds(5),
      _exitStatus(0), _exited(false) {}

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

// --- Non-blocking API ---

bool CGIHandler::start(const std::string& scriptPath,
                       const std::string& interpreterPath,
                       const std::vector<std::string>& env,
                       const std::string& input,
                       int timeoutSeconds) {
    cleanup();

    _input = input;
    _inputPos = 0;
    _bodyFileFd = -1;
    _output.clear();
    _error.clear();
    _exitStatus = 0;
    _exited = false;
    _timeoutSeconds = timeoutSeconds;

    std::vector<std::string> argv;
    if (!interpreterPath.empty()) {
        argv.push_back(interpreterPath);
    }
    argv.push_back(scriptPath);

    ProcessSpawner spawner;
    _pid = spawner.spawn(argv, env, _stdinFd, _stdoutFd, _stderrFd);
    if (_pid < 0) {
        _error = "CGIHandler::start failed to spawn process";
        _state = CGI_ERROR;
        return false;
    }

    // Set all pipe FDs to non-blocking.
    fcntl(_stdinFd, F_SETFL, O_NONBLOCK);
    fcntl(_stdoutFd, F_SETFL, O_NONBLOCK);
    fcntl(_stderrFd, F_SETFL, O_NONBLOCK);

    _startTime = time(NULL);

    // If there is no input to write, close stdin immediately.
    if (_input.empty()) {
        closeFd(_stdinFd);
        _state = CGI_READING;
    } else {
        _state = CGI_WRITING;
    }

    return true;
}

bool CGIHandler::startFromFile(const std::string& scriptPath,
                               const std::string& interpreterPath,
                               const std::vector<std::string>& env,
                               const std::string& bodyFilePath,
                               int timeoutSeconds) {
    cleanup();

    _input.clear();
    _inputPos = 0;
    _bodyFileFd = -1;
    _output.clear();
    _error.clear();
    _exitStatus = 0;
    _exited = false;
    _timeoutSeconds = timeoutSeconds;

    // Open the body file for streaming if a path was provided.
    if (!bodyFilePath.empty()) {
        struct stat st;
        if (stat(bodyFilePath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            _error = "CGIHandler::startFromFile: body file not found";
            _state = CGI_ERROR;
            return false;
        }
        _bodyFileFd = open(bodyFilePath.c_str(), O_RDONLY);
        if (_bodyFileFd < 0) {
            _error = "CGIHandler::startFromFile: failed to open body file";
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
        _error = "CGIHandler::startFromFile failed to spawn process";
        _state = CGI_ERROR;
        return false;
    }

    // Set all pipe FDs to non-blocking.
    fcntl(_stdinFd, F_SETFL, O_NONBLOCK);
    fcntl(_stdoutFd, F_SETFL, O_NONBLOCK);
    fcntl(_stderrFd, F_SETFL, O_NONBLOCK);

    _startTime = time(NULL);

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
                                  int timeoutSeconds)
{
    EnvBuilder envBuilder;
    std::vector<std::string> envVars = envBuilder.buildFromRequest(req, config);

    std::string resolvedScript = scriptPath;
    char resolvedBuf[4096];
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

    std::string bodyFilePath = req.getBodyFilePath();
    if (!bodyFilePath.empty()) {
        return startFromFile(resolvedScript, interpreterPath, envVars, bodyFilePath, timeoutSeconds);
    } else {
        return start(resolvedScript, interpreterPath, envVars, req.getBody(), timeoutSeconds);
    }
}

void CGIHandler::onStdinReady() {
    if (_stdinFd < 0 || _state == CGI_DONE || _state == CGI_ERROR) return;

    // If the in-memory buffer is exhausted, try to refill from the body file.
    if (_bodyFileFd >= 0 && _inputPos >= _input.size()) {
        char buf[8192];
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

    char buf[4096];
    ssize_t n = read(_stdoutFd, buf, sizeof(buf));
    if (n > 0) {
        _output.append(buf, static_cast<size_t>(n));
    } else if (n == 0) {
        // EOF — CGI closed stdout.
        closeFd(_stdoutFd);
        checkDone();
    } else if (errno != EAGAIN && errno != EINTR) {
        closeFd(_stdoutFd);
        checkDone();
    }
}

void CGIHandler::onStderrReady() {
    if (_stderrFd < 0 || _state == CGI_DONE || _state == CGI_ERROR) return;

    char buf[4096];
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

bool CGIHandler::checkTimeout() {
    if (_state == CGI_DONE || _state == CGI_ERROR || _state == CGI_IDLE) return false;
    if (_timeoutSeconds <= 0) return false;

    if ((time(NULL) - _startTime) >= _timeoutSeconds) {
        if (_pid > 0 && !_exited) {
            kill(_pid, SIGKILL);
            waitpid(_pid, &_exitStatus, 0);
            _exited = true;
        }
        closeFd(_stdinFd);
        closeFd(_stdoutFd);
        closeFd(_stderrFd);
        _error = "CGIHandler: process timed out";
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
const std::string& CGIHandler::getOutput() const { return _output; }
const std::string& CGIHandler::getError() const { return _error; }

bool CGIHandler::succeeded() const {
    return _state == CGI_DONE && _exited
           && WIFEXITED(_exitStatus) && WEXITSTATUS(_exitStatus) == 0;
}

// --- Blocking API (backward-compatible wrapper for tests) ---

bool CGIHandler::run(const std::string& scriptPath,
                     const std::string& interpreterPath,
                     const std::vector<std::string>& env,
                     const std::string& input,
                     std::string& output,
                     std::string& error,
                     int timeoutSeconds) {
    if (!start(scriptPath, interpreterPath, env, input, timeoutSeconds)) {
        output = _output;
        error = _error;
        return false;
    }

    while (_state != CGI_DONE && _state != CGI_ERROR) {
        if (checkTimeout()) break;

        struct pollfd fds[3];
        int nfds = 0;
        int stdinIdx = -1, stdoutIdx = -1, stderrIdx = -1;

        if (_stdinFd >= 0) {
            stdinIdx = nfds;
            fds[nfds].fd = _stdinFd;
            fds[nfds].events = POLLOUT;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (_stdoutFd >= 0) {
            stdoutIdx = nfds;
            fds[nfds].fd = _stdoutFd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (_stderrFd >= 0) {
            stderrIdx = nfds;
            fds[nfds].fd = _stderrFd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }

        if (nfds == 0) break;

        int pollRes = poll(fds, nfds, 100);
        if (pollRes < 0 && errno != EINTR) break;

        if (stdinIdx >= 0 && (fds[stdinIdx].revents & POLLOUT))
            onStdinReady();
        if (stdoutIdx >= 0 && (fds[stdoutIdx].revents & (POLLIN | POLLHUP)))
            onStdoutReady();
        if (stderrIdx >= 0 && (fds[stderrIdx].revents & (POLLIN | POLLHUP)))
            onStderrReady();

        tryReap();
    }

    output = _output;
    error = _error;
    return succeeded();
}
