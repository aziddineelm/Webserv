#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include <vector>
#include <map>
#include <time.h>

class Request;
class ServerConfig;

// State of a CGI process managed by CGIHandler.
enum CgiState {
    CGI_IDLE,           // No CGI process running
    CGI_WRITING,        // Writing request body to CGI stdin
    CGI_READING,        // Reading CGI stdout/stderr (stdin already closed)
    CGI_HEADERS_READY,  // CGI headers parsed, body is streaming
    CGI_DONE,           // CGI finished successfully
    CGI_ERROR           // CGI failed (timeout, spawn error, etc.)
};

// Non-blocking CGI handler designed to integrate with an external event loop.
//
// Usage (non-blocking, for the main server loop):
//   1. Call start() to fork/exec the CGI script.
//   2. Register getStdinFd(), getStdoutFd(), getStderrFd() with your poll() set.
//   3. When poll() says a FD is ready, call the corresponding on*Ready() method.
//   4. Periodically call checkTimeout() to enforce the time limit.
//   5. When headersReady() returns true, start streaming body via popOutput().
//   6. When getState() returns CGI_DONE or CGI_ERROR, read getOutput()/getError().
//
// Usage (blocking, for tests):
//   Call run() which wraps all of the above in a local poll() loop.
class CGIHandler {
public:
    CGIHandler();
    ~CGIHandler();

    // --- Non-blocking API (for main event loop integration) ---

    // Start the CGI process with an in-memory body string.
    // Suitable for small POST bodies. Returns true on success.
    // idleTimeoutSec: nginx-style inactivity timeout (0 = disabled).
    // maxTimeoutSec:  php-fpm-style absolute timeout (0 = disabled).
    bool start(const std::string& scriptPath,
               const std::string& interpreterPath,
               const std::vector<std::string>& env,
               const std::string& input,
               int idleTimeoutSec = 30,
               int maxTimeoutSec = 0);

    // Start the CGI process, streaming the POST body from a file on disk.
    // This avoids loading the entire body into RAM, preventing OOM on
    // large uploads (e.g. 5GB video files).
    // Pass an empty bodyFilePath to skip body input entirely.
    bool startFromFile(const std::string& scriptPath,
                       const std::string& interpreterPath,
                       const std::vector<std::string>& env,
                       const std::string& bodyFilePath,
                       int idleTimeoutSec = 30,
                       int maxTimeoutSec = 0);

    // High-level wrapper: prepares environment, resolves symlinks, and starts CGI from an HTTP Request.
    bool startFromRequest(const Request& req,
                          const ServerConfig& config,
                          const std::string& scriptPath,
                          const std::string& interpreterPath);

    // Called by the main loop when poll() reports stdinFd is writable.
    void onStdinReady();

    // Called by the main loop when poll() reports stdoutFd is readable.
    void onStdoutReady();

    // Called by the main loop when poll() reports stderrFd is readable.
    void onStderrReady();

    // Check if the CGI has exceeded its timeout (idle or absolute).
    // Returns true if timed out. Should be called periodically.
    bool checkTimeout();

    // Kill the process if still running, close any open FDs, reap child.
    void cleanup();

    // --- Getters ---

    // File descriptors for poll() registration. Returns -1 if closed/unused.
    int getStdinFd() const;
    int getStdoutFd() const;
    int getStderrFd() const;

    CgiState getState() const;
    const std::string& getOutput() const;
    const std::string& getError() const;

    // True if CGI process exited with status 0.
    bool succeeded() const;

    // --- Streaming API ---

    // Returns true once CGI headers have been parsed from stdout.
    bool headersReady() const;

    // Access the parsed CGI headers (valid after headersReady() == true).
    const std::map<std::string, std::string>& getCgiHeaders() const;

    // Returns true if there are body bytes waiting to be sent to the client.
    bool hasPendingOutput() const;

    // Pop up to maxBytes from the output queue. Returns empty string if nothing available.
    std::string popOutput(size_t maxBytes = 8192);

    // Returns true when stdout is closed AND all queued output has been popped.
    bool outputFullyConsumed() const;

    // --- Blocking API (backward-compatible, for tests) ---

    // Runs the full CGI lifecycle in a blocking poll() loop.
    // This is a convenience wrapper around the non-blocking API.
    // Default idle timeout is 5s for tests (production uses 30s via start()).
    bool run(const std::string& scriptPath,
             const std::string& interpreterPath,
             const std::vector<std::string>& env,
             const std::string& input,
             std::string& output,
             std::string& error,
             int idleTimeoutSec = 5,
             int maxTimeoutSec = 0);

private:
    CgiState _state;
    int _pid;
    int _stdinFd;
    int _stdoutFd;
    int _stderrFd;

    std::string _input;      // In-memory body buffer (small bodies or current chunk)
    size_t _inputPos;
    std::string _output;     // Full accumulated output (only used in blocking mode)
    std::string _error;

    int _bodyFileFd;         // FD for streaming body from temp file (-1 if unused)

    time_t _startTime;
    int _exitStatus;
    bool _exited;

    // --- Streaming state ---
    bool _headersParsed;
    bool _streamingMode;              // true = EventLoop path (skip _output accumulation)
    std::string _rawBuffer;           // Accumulates stdout until headers are found
    std::map<std::string, std::string> _cgiHeaders;  // Parsed CGI headers
    std::string _outputQueue;         // Body bytes ready for EventLoop to consume
    size_t _outputQueueOffset;        // Offset into _outputQueue (avoids O(n) erase)

    // --- Dual timeout state ---
    time_t _lastActivityTime;         // Updated on every successful read()
    int _idleTimeoutSeconds;          // Inactivity timeout (nginx-style, default 30)
    int _maxTimeoutSeconds;           // Absolute timeout (php-fpm-style, 0 = disabled)

    // --- Private helpers ---

    // Try to reap the child process (non-blocking). Updates _exited/_exitStatus.
    void tryReap();

    // Check if all output pipes are closed and transition to CGI_DONE if so.
    void checkDone();

    // Close a file descriptor and set it to -1.
    void closeFd(int& fd);

    // Detect \r\n\r\n in _rawBuffer and parse CGI headers using CGIResponseParser.
    void _tryParseHeaders();

    // Reclaim memory when _outputQueueOffset exceeds half of _outputQueue.
    void _compactOutputQueue();

    // int to string helper (no C++11 std::to_string).
    static std::string _intToStr(int n);
};

#endif
