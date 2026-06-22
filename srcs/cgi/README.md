# CGI Module

This module is responsible for executing Common Gateway Interface (CGI) scripts (like Python or PHP) and capturing their output. It is designed to be fully non-blocking and to integrate cleanly with the main web server's `poll()` or `epoll()` event loop.

## Architecture

The CGI pipeline is divided into several specialized classes:

*   **`EnvBuilder`**: Constructs the environment variables (`KEY=VALUE` strings) required by the CGI specification (RFC 3875). It translates HTTP request metadata into variables like `REQUEST_METHOD`, `QUERY_STRING`, `PATH_INFO`, `PATH_TRANSLATED`, and `HTTP_*` headers.
*   **`ProcessSpawner`**: A low-level utility that handles the actual `fork()`, wiring up `pipe()` file descriptors to `stdin`, `stdout`, and `stderr` using `dup2()`, changing the working directory via `chdir()`, and finally calling `execve()`.
*   **`CGIHandler`**: The main orchestrator and state machine. It manages the lifecycle of the CGI process, handles non-blocking I/O across the pipes, enforces timeouts, and reaps the child process.
*   **`TempFile`**: A simple utility for creating temporary files, useful if request bodies are too large to hold in memory.

## Integration Guide (For the Core Server Loop)

The `CGIHandler` is built as an asynchronous state machine. It must **not** be allowed to block the server. 

When an HTTP request routes to a CGI script, follow these steps to integrate it into your main event loop:

### 1. Preparation
Before invoking the CGI module, the HTTP parser layer MUST:
1.  **Unchunk the body**: If the request used `Transfer-Encoding: chunked`, decode it into a flat raw byte stream. The CGI script expects raw data, not HTTP chunking syntax.
2.  **Calculate Size**: Calculate the exact byte size of this decoded body so `EnvBuilder` can set the `CONTENT_LENGTH` environment variable.

### 2. Spawning the Process
Instantiate a `CGIHandler` and call `start()`. This forks the process and returns immediately.
```cpp
CGIHandler cgi;
bool success = cgi.start(scriptPath, interpreterPath, envVars, requestBody, 5); // 5 sec timeout
```

### 3. Registering File Descriptors
Retrieve the pipe file descriptors and add them to your main `poll()` set.
```cpp
int stdinFd = cgi.getStdinFd();   // Add to poll() with POLLOUT
int stdoutFd = cgi.getStdoutFd(); // Add to poll() with POLLIN
int stderrFd = cgi.getStderrFd(); // Add to poll() with POLLIN
```
*Note: If `getStdinFd()` returns `-1`, it means there was no request body and the pipe is already closed. Do not poll it.*

### 4. Processing I/O
Inside your main `poll()` loop, when an event fires on one of the CGI FDs, call the corresponding handler. These methods will read or write a single chunk and return immediately.
```cpp
if (pollEvent.fd == cgi.getStdoutFd() && (pollEvent.revents & POLLIN)) {
    cgi.onStdoutReady();
}
// Do the same for onStderrReady() and onStdinReady()
```

### 5. Enforcing Timeouts
At the top of your main event loop, call `checkTimeout()`. If a script runs too long, this will kill the process and transition the state to `CGI_ERROR`.
```cpp
cgi.checkTimeout();
```

### 6. Completion
Check the state of the handler. Once it reaches `CGI_DONE` or `CGI_ERROR`, the process is finished.
```cpp
if (cgi.getState() == CGI_DONE) {
    if (cgi.succeeded()) {
        std::string output = cgi.getOutput();
        // Parse headers and body using CGIResponseParser, then build HTTP response.
    } else {
        // Return HTTP 500 Internal Server Error
    }
}
```

## Testing
A backward-compatible, blocking `run()` method is included in `CGIHandler` purely for standalone testing. You can run the test suite via:
```bash
make cgi-test
```
