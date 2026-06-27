#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include <vector>
#include <time.h>

// State of a CGI process managed by CGIHandler.
enum CgiState {
    CGI_IDLE,       // No CGI process running
    CGI_WRITING,    // Writing request body to CGI stdin
    CGI_READING,    // Reading CGI stdout/stderr (stdin already closed)
    CGI_DONE,       // CGI finished successfully
    CGI_ERROR       // CGI failed (timeout, spawn error, etc.)
};

// Non-blocking CGI handler designed to integrate with an external event loop.
//
// Usage (non-blocking, for the main server loop):
//   1. Call start() to fork/exec the CGI script.
//   2. Register getStdinFd(), getStdoutFd(), getStderrFd() with your poll() set.
//   3. When poll() says a FD is ready, call the corresponding on*Ready() method.
//   4. Periodically call checkTimeout() to enforce the time limit.
//   5. When getState() returns CGI_DONE or CGI_ERROR, read getOutput()/getError().
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
    // After this call, use getFd methods to register with poll().
    bool start(const std::string& scriptPath,
               const std::string& interpreterPath,
               const std::vector<std::string>& env,
               const std::string& input,
               int timeoutSeconds = 5);

    // Start the CGI process, streaming the POST body from a file on disk.
    // This avoids loading the entire body into RAM, preventing OOM on
    // large uploads (e.g. 5GB video files).
    // Pass an empty bodyFilePath to skip body input entirely.
    bool startFromFile(const std::string& scriptPath,
                       const std::string& interpreterPath,
                       const std::vector<std::string>& env,
                       const std::string& bodyFilePath,
                       int timeoutSeconds = 5);

    // Called by the main loop when poll() reports stdinFd is writable.
    void onStdinReady();

    // Called by the main loop when poll() reports stdoutFd is readable.
    void onStdoutReady();

    // Called by the main loop when poll() reports stderrFd is readable.
    void onStderrReady();

    // Check if the CGI has exceeded its timeout. Returns true if timed out.
    // Should be called periodically (e.g., each iteration of the main loop).
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

    // --- Blocking API (backward-compatible, for tests) ---

    // Runs the full CGI lifecycle in a blocking poll() loop.
    // This is a convenience wrapper around the non-blocking API.
    bool run(const std::string& scriptPath,
             const std::string& interpreterPath,
             const std::vector<std::string>& env,
             const std::string& input,
             std::string& output,
             std::string& error,
             int timeoutSeconds = 5);

private:
    CgiState _state;
    int _pid;
    int _stdinFd;
    int _stdoutFd;
    int _stderrFd;

    std::string _input;      // In-memory body buffer (small bodies or current chunk)
    size_t _inputPos;
    std::string _output;
    std::string _error;

    int _bodyFileFd;         // FD for streaming body from temp file (-1 if unused)

    time_t _startTime;
    int _timeoutSeconds;
    int _exitStatus;
    bool _exited;

    // Try to reap the child process (non-blocking). Updates _exited/_exitStatus.
    void tryReap();

    // Check if all output pipes are closed and transition to CGI_DONE if so.
    void checkDone();

    // Close a file descriptor and set it to -1.
    void closeFd(int& fd);
};

#endif
