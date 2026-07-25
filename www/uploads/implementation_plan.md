# Fix CGI Streaming — Person C Implementation Plan (v2)

## Problem

The server buffers the **entire** CGI stdout output into a single string (`_output`), waits for the CGI process to exit (`CGI_DONE`), then parses headers and builds the full HTTP response in one shot. This means:

- A CGI script that prints progress over 30 seconds → the client sees **nothing** for 30 seconds, then everything at once.
- Server-Sent Events (SSE), chunked downloads, and large dynamic responses are broken.
- Memory usage spikes for large CGI outputs (entire response held in RAM before any byte is sent).

Additionally, the current timeout is a **single absolute timer** (`_timeoutSeconds`), which means a long-running but actively-streaming CGI (like SSE) will always be killed.

## Current Data Flow (Broken)

```
CGI stdout → onStdoutReady() → append to _output string → wait for EOF
                                                              ↓
                                                         CGI_DONE
                                                              ↓
                                              buildFromCgiOutput(_output)
                                                              ↓
                                              Parse headers + set Content-Length
                                                              ↓
                                              _startWriting → send to client
```

## Target Data Flow (Fixed)

```
CGI stdout → onStdoutReady() → buffer in _rawBuffer
                                    ↓
                         ┌── Headers not parsed yet?
                         │      Search for \r\n\r\n in _rawBuffer
                         │      Found → reuse CGIResponseParser, push leftover body to _outputQueue
                         │      State becomes CGI_HEADERS_READY
                         │      _lastActivityTime is updated
                         │
                         └── Headers already parsed?
                                Push raw bytes directly to _outputQueue
                                _lastActivityTime is updated
                                    ↓
                         EventLoop detects hasPendingOutput() == true
                                    ↓
                         Pops chunks → sends to client (EPOLLOUT)
                                    ↓
                         On EOF → final flush + done
```

---

## Design Decisions (Best Practices Chosen)

These are the choices made from the review, with rationale:

| Issue | Choice | Rationale |
|-------|--------|-----------|
| Header parsing duplication | **Reuse `CGIResponseParser::parse()`** | DRY — avoids subtle bugs (missing case-insensitive Status matching, missing `trimStatusValue()`) |
| Unbounded `_rawBuffer` | **64KB hard limit, then `CGI_ERROR`** | Prevents memory bombs from malicious/buggy scripts |
| Dual memory buffering | **`_streamingMode` flag** — skip `_output` append in streaming path | Halves memory for large responses; blocking `run()` still works |
| `popOutput()` O(n) erase | **Offset-based approach** (like existing `_inputPos` pattern) | Consistent with codebase style, avoids quadratic behavior |
| Timeout model | **Dual: inactivity (30s) + optional absolute from config** | nginx-style inactivity lets SSE work; absolute timeout catches runaway scripts like php-fpm |

---

## Proposed Changes

### 1. Dual Timeout System — Config + CGIHandler

The timeout system works as follows:
- **Inactivity timeout** (default 30s, always active): Reset every time `onStdoutReady()` reads data. If no data arrives for 30s, kill the process. This is nginx's `proxy_read_timeout` behavior.
- **Absolute timeout** (optional, from config): A hard wall-clock limit. If set, the CGI is killed after this many seconds regardless of activity. This is php-fpm's `request_terminate_timeout` behavior.

Config file syntax (in a `location` block):
```nginx
location /cgi-bin {
    cgi_extension .py .php;
    cgi_path /usr/bin/python3;
    cgi_idle_timeout 30;        # Inactivity timeout (default: 30s)
    cgi_max_timeout  300;       # Absolute timeout (default: 0 = disabled)
}
```

#### [MODIFY] [ServerConfig.hpp](file:///home/anbaya/Desktop/Webserv/srcs/config/ServerConfig.hpp)

Add two new fields to `LocationConfig`:

```diff
     std::map<std::string, std::string> cgi_map;
     size_t client_max_body_size;
     std::map<int, std::string> error_pages;
+    int cgi_idle_timeout;    // Inactivity timeout in seconds (default: 30, 0 = disabled)
+    int cgi_max_timeout;     // Absolute timeout in seconds (default: 0 = disabled)

     LocationConfig();
```

#### [MODIFY] [ServerConfig.cpp](file:///home/anbaya/Desktop/Webserv/srcs/config/ServerConfig.cpp)

Initialize defaults in the constructor:

```diff
-LocationConfig::LocationConfig() : autoindex(false), redirect_code(0), client_max_body_size(1048576) {}
+LocationConfig::LocationConfig() : autoindex(false), redirect_code(0), client_max_body_size(1048576),
+    cgi_idle_timeout(30), cgi_max_timeout(0) {}
```

#### [MODIFY] [ConfigParser.cpp](file:///home/anbaya/Desktop/Webserv/srcs/config/ConfigParser.cpp)

Add parsing for the two new directives (after the `cgi_path` block, around line 268):

```cpp
        } else if (directive == "cgi_idle_timeout") {
            if (args.empty()) throw ConfigException("cgi_idle_timeout directive missing arguments");
            if (!isNumber(args[0])) throw ConfigException("Invalid cgi_idle_timeout: " + args[0]);
            newLocation.cgi_idle_timeout = std::atoi(args[0].c_str());
        } else if (directive == "cgi_max_timeout") {
            if (args.empty()) throw ConfigException("cgi_max_timeout directive missing arguments");
            if (!isNumber(args[0])) throw ConfigException("Invalid cgi_max_timeout: " + args[0]);
            newLocation.cgi_max_timeout = std::atoi(args[0].c_str());
```

Also add the directive names to the `isLocationDirective` validation (line 59):

```diff
-               value == "cgi_extension" || value == "cgi_path" || value == "upload_store" ||
+               value == "cgi_extension" || value == "cgi_path" ||
+               value == "cgi_idle_timeout" || value == "cgi_max_timeout" ||
+               value == "upload_store" ||
```

---

### 2. New CGI States + Streaming Members

#### [MODIFY] [CGIHandler.hpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.hpp)

Add new state, streaming API, and dual-timeout fields:

```diff
 enum CgiState {
     CGI_IDLE,
     CGI_WRITING,
     CGI_READING,
+    CGI_HEADERS_READY,  // CGI headers parsed, body is streaming
     CGI_DONE,
     CGI_ERROR
 };
```

Add new includes at the top:

```diff
 #include <string>
 #include <vector>
+#include <map>
 #include <time.h>
```

Add new public API and private members:

```diff
 // --- Getters ---
 CgiState getState() const;
 const std::string& getOutput() const;
 const std::string& getError() const;
 bool succeeded() const;

+// --- Streaming API ---
+
+// Returns true once CGI headers have been parsed from stdout.
+bool headersReady() const;
+
+// Access the parsed CGI headers (valid after headersReady() == true).
+const std::map<std::string, std::string>& getCgiHeaders() const;
+
+// Returns true if there are body bytes waiting to be sent to the client.
+bool hasPendingOutput() const;
+
+// Pop up to maxBytes from the output queue. Returns empty string if nothing available.
+std::string popOutput(size_t maxBytes = 8192);
+
+// Returns true when stdout is closed AND all queued output has been popped.
+bool outputFullyConsumed() const;

 private:
     CgiState _state;
     ...
+    // --- Streaming state ---
+    bool _headersParsed;
+    bool _streamingMode;              // true when driven by EventLoop (skip _output accumulation)
+    std::string _rawBuffer;           // Accumulates stdout until headers are found
+    std::map<std::string, std::string> _cgiHeaders;  // Parsed CGI headers
+    std::string _outputQueue;         // Body bytes ready for EventLoop to consume
+    size_t _outputQueueOffset;        // Offset into _outputQueue (avoids O(n) erase)
+
+    // --- Dual timeout state ---
+    time_t _lastActivityTime;         // Updated on every successful read()
+    int _idleTimeoutSeconds;          // Inactivity timeout (nginx-style, default 30)
+    int _maxTimeoutSeconds;           // Absolute timeout (php-fpm-style, 0 = disabled)
+
+    // --- Private helpers ---
+    void _tryParseHeaders();          // Detect \r\n\r\n and parse CGI headers
+    void _compactOutputQueue();       // Reclaim memory when offset > half of queue
+    static std::string _intToStr(int n); // int to string (no C++11)
```

Update `start()` / `startFromFile()` / `startFromRequest()` signatures to accept dual timeouts:

```diff
     bool start(const std::string& scriptPath,
                const std::string& interpreterPath,
                const std::vector<std::string>& env,
                const std::string& input,
-               int timeoutSeconds = 5);
+               int idleTimeoutSec = 30,
+               int maxTimeoutSec = 0);

     bool startFromFile(const std::string& scriptPath,
                        const std::string& interpreterPath,
                        const std::vector<std::string>& env,
                        const std::string& bodyFilePath,
-                       int timeoutSeconds = 5);
+                       int idleTimeoutSec = 30,
+                       int maxTimeoutSec = 0);

     bool startFromRequest(const Request& req,
                           const ServerConfig& config,
                           const std::string& scriptPath,
                           const std::string& interpreterPath,
-                          int timeoutSeconds = 5);
+                          int idleTimeoutSec = 30,
+                          int maxTimeoutSec = 0);
```

Update blocking `run()` similarly:

```diff
     bool run(const std::string& scriptPath,
              const std::string& interpreterPath,
              const std::vector<std::string>& env,
              const std::string& input,
              std::string& output,
              std::string& error,
-             int timeoutSeconds = 5);
+             int idleTimeoutSec = 5,
+             int maxTimeoutSec = 0);
```

> [!NOTE]
> `run()` keeps default idle timeout at 5s (for tests), while `start()`/`startFromFile()` use 30s (for production). This matches current test behavior.

---

### 3. Modified `onStdoutReady()` — Incremental Parsing + Activity Tracking

#### [MODIFY] [CGIHandler.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.cpp)

Add required includes:

```diff
 #include "CGIHandler.hpp"
 #include "ProcessSpawner.hpp"
+#include "CGIResponseParser.hpp"

 #include <unistd.h>
 #include <poll.h>
 #include <sys/wait.h>
 #include <fcntl.h>
 #include <errno.h>
 #include <signal.h>
 #include <time.h>
 #include <sys/stat.h>
 #include <stdlib.h>
+#include <sstream>
 #include "EnvBuilder.hpp"
```

Replace the current `onStdoutReady()`:

```cpp
void CGIHandler::onStdoutReady() {
    if (_stdoutFd < 0 || _state == CGI_DONE || _state == CGI_ERROR) return;

    char buf[4096];
    ssize_t n = read(_stdoutFd, buf, sizeof(buf));
    if (n > 0) {
        // Update inactivity timer on every successful read
        _lastActivityTime = time(NULL);

        if (!_headersParsed) {
            // Phase 1: Accumulating data until we find header/body separator
            _rawBuffer.append(buf, static_cast<size_t>(n));

            // Safety: reject CGI output with absurdly large headers
            if (_rawBuffer.size() > 65536) {
                _error = "CGIHandler: CGI headers exceeded 64KB limit";
                _state = CGI_ERROR;
                closeFd(_stdoutFd);
                return;
            }

            _tryParseHeaders();
        } else {
            // Phase 2: Headers parsed — body bytes go straight to output queue
            _outputQueue.append(buf, static_cast<size_t>(n));
        }

        // Backward compat: blocking run() still uses _output
        if (!_streamingMode) {
            _output.append(buf, static_cast<size_t>(n));
        }
    } else if (n == 0) {
        // EOF
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
```

New private helper — reuses `CGIResponseParser` for correct header parsing:

```cpp
void CGIHandler::_tryParseHeaders() {
    // Check if separator exists in buffer
    size_t sepPos = _rawBuffer.find("\r\n\r\n");
    size_t sepLen = 4;
    if (sepPos == std::string::npos) {
        sepPos = _rawBuffer.find("\n\n");
        sepLen = 2;
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
```

Output queue helper — compact when offset exceeds half the string:

```cpp
void CGIHandler::_compactOutputQueue() {
    if (_outputQueueOffset > 0 && _outputQueueOffset > _outputQueue.size() / 2) {
        _outputQueue.erase(0, _outputQueueOffset);
        _outputQueueOffset = 0;
    }
}
```

Int-to-string helper (no C++11):

```cpp
std::string CGIHandler::_intToStr(int n) {
    std::ostringstream oss;
    oss << n;
    return oss.str();
}
```

---

### 4. Streaming Getters (Offset-Based)

#### [MODIFY] [CGIHandler.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.cpp)

```cpp
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
```

---

### 5. Dual Timeout — `checkTimeout()` Rewrite

#### [MODIFY] [CGIHandler.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.cpp)

Replace the current `checkTimeout()`:

```cpp
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
```

---

### 6. Initialize New Members + Updated Signatures

#### [MODIFY] [CGIHandler.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.cpp)

Constructor:

```diff
 CGIHandler::CGIHandler()
     : _state(CGI_IDLE), _pid(-1),
       _stdinFd(-1), _stdoutFd(-1), _stderrFd(-1),
       _inputPos(0), _bodyFileFd(-1),
       _startTime(0), _timeoutSeconds(5),
-      _exitStatus(0), _exited(false) {}
+      _exitStatus(0), _exited(false),
+      _headersParsed(false), _streamingMode(false),
+      _outputQueueOffset(0),
+      _lastActivityTime(0), _idleTimeoutSeconds(30), _maxTimeoutSeconds(0) {}
```

Updated `start()` signature and reset logic:

```diff
 bool CGIHandler::start(const std::string& scriptPath,
                        const std::string& interpreterPath,
                        const std::vector<std::string>& env,
                        const std::string& input,
-                       int timeoutSeconds) {
+                       int idleTimeoutSec,
+                       int maxTimeoutSec) {
     cleanup();

     _input = input;
     _inputPos = 0;
     _bodyFileFd = -1;
     _output.clear();
     _error.clear();
     _exitStatus = 0;
     _exited = false;
-    _timeoutSeconds = timeoutSeconds;
+    _idleTimeoutSeconds = idleTimeoutSec;
+    _maxTimeoutSeconds = maxTimeoutSec;
+    _headersParsed = false;
+    _streamingMode = true;   // EventLoop path — don't double-buffer in _output
+    _rawBuffer.clear();
+    _cgiHeaders.clear();
+    _outputQueue.clear();
+    _outputQueueOffset = 0;
```

Same pattern for `startFromFile()`. After process spawn, set `_lastActivityTime`:

```diff
     _startTime = time(NULL);
+    _lastActivityTime = _startTime;
```

Updated `startFromRequest()` — reads timeouts from config:

```cpp
bool CGIHandler::startFromRequest(const Request& req,
                                  const ServerConfig& config,
                                  const std::string& scriptPath,
                                  const std::string& interpreterPath,
                                  int idleTimeoutSec,
                                  int maxTimeoutSec)
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
        return startFromFile(resolvedScript, interpreterPath, envVars, bodyFilePath, idleTimeoutSec, maxTimeoutSec);
    } else {
        return start(resolvedScript, interpreterPath, envVars, req.getBody(), idleTimeoutSec, maxTimeoutSec);
    }
}
```

---

### 7. Update Blocking `run()` for New Signature

#### [MODIFY] [CGIHandler.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.cpp)

```diff
 bool CGIHandler::run(const std::string& scriptPath,
                      const std::string& interpreterPath,
                      const std::vector<std::string>& env,
                      const std::string& input,
                      std::string& output,
                      std::string& error,
-                     int timeoutSeconds) {
-    if (!start(scriptPath, interpreterPath, env, input, timeoutSeconds)) {
+                     int idleTimeoutSec,
+                     int maxTimeoutSec) {
+    if (!start(scriptPath, interpreterPath, env, input, idleTimeoutSec, maxTimeoutSec)) {
         output = _output;
         error = _error;
         return false;
     }
+    _streamingMode = false; // Blocking mode — accumulate in _output
```

The rest of `run()` stays the same.

---

### 8. EventLoop — Pass Config Timeouts to CGI

#### [MODIFY] [EventLoop.cpp](file:///home/anbaya/Desktop/Webserv/srcs/server/EventLoop.cpp)

Update `_spawnCgi()` to read timeouts from the matched `LocationConfig`:

```diff
 void EventLoop::_spawnCgi(int clientFd, Client &client, const ServerConfig &serverConfig) {
-    bool started = client.cgi.startFromRequest(client.request, serverConfig, client.response.getCgiScript(), client.response.getCgiInterpreter(), CGI_TIMEOUT_SEC);
+    // Look up location-specific CGI timeouts from config
+    const LocationConfig* loc = serverConfig.matchLocation(client.request.getUri());
+    int idleTimeout = loc ? loc->cgi_idle_timeout : 30;
+    int maxTimeout  = loc ? loc->cgi_max_timeout  : 0;
+
+    bool started = client.cgi.startFromRequest(client.request, serverConfig,
+        client.response.getCgiScript(), client.response.getCgiInterpreter(),
+        idleTimeout, maxTimeout);
```

Remove or keep the `CGI_TIMEOUT_SEC` define as a fallback default (no longer used directly).

---

## Backward Compatibility

> [!IMPORTANT]
> **No existing test needs modification.** The blocking `run()` path:
> - Still accumulates output in `_output` (because `_streamingMode = false`)
> - Default idle timeout stays 5s for `run()` (matches current test behavior)
> - `getOutput()` returns the full response as before
>
> The new streaming API (`popOutput()`, `headersReady()`) is purely additive.

---

## What Person A and Person B Need to Change (Coordination)

### Person A — [EventLoop.cpp](file:///home/anbaya/Desktop/Webserv/srcs/server/EventLoop.cpp)

The current `_handleCgiReady()` at [L404-L421](file:///home/anbaya/Desktop/Webserv/srcs/server/EventLoop.cpp#L404-L421) only acts when `CGI_DONE`/`CGI_ERROR`. Person A needs to add a check for `CGI_HEADERS_READY`:

```
After calling onStdoutReady():
  if cgi.headersReady() && client.state == STATE_CGI_RUNNING:
    → Build Response headers from cgi.getCgiHeaders()
    → Set Transfer-Encoding: chunked (no Content-Length)
    → Send HTTP headers to client
    → Change client.state to STATE_CGI_STREAMING (new state)
    → Re-register clientFd for EPOLLOUT

  if cgi.hasPendingOutput() && client.state == STATE_CGI_STREAMING:
    → Pop chunks from cgi.popOutput()
    → Append chunked-encoded data to client.writeBuffer
    → Let existing _handleWrite() flush it

  if cgi.getState() == CGI_DONE && cgi.outputFullyConsumed():
    → Send final chunk "0\r\n\r\n"
    → Transition to keep-alive or close
```

Person A also needs to **stop calling `_removeEpollFd(clientFd)`** at [L263](file:///home/anbaya/Desktop/Webserv/srcs/server/EventLoop.cpp#L263) and instead keep `clientFd` registered so data can be flushed while the CGI is still running.

### Person B — [response.cpp](file:///home/anbaya/Desktop/Webserv/srcs/http/response/response.cpp)

Person B needs to support building a response from pre-parsed headers (a `std::map`) without requiring the full body upfront, and support `Transfer-Encoding: chunked` mode where `Content-Length` is not set.

---

## Files Changed Summary (Person C Only)

| File | Action | What Changes |
|------|--------|------|
| [CGIHandler.hpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.hpp) | MODIFY | Add `CGI_HEADERS_READY` state, `#include <map>`, new streaming API methods, dual timeout fields, new private members/helpers |
| [CGIHandler.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIHandler.cpp) | MODIFY | Add `#include <sstream>` + `CGIResponseParser.hpp`, rewrite `onStdoutReady()` with 64KB guard, add `_tryParseHeaders()` (reuses `CGIResponseParser`), implement offset-based `popOutput()`, rewrite `checkTimeout()` for dual model, update constructor + `start()`/`startFromFile()`/`startFromRequest()`/`run()` signatures |
| [ServerConfig.hpp](file:///home/anbaya/Desktop/Webserv/srcs/config/ServerConfig.hpp) | MODIFY | Add `cgi_idle_timeout` and `cgi_max_timeout` to `LocationConfig` |
| [ServerConfig.cpp](file:///home/anbaya/Desktop/Webserv/srcs/config/ServerConfig.cpp) | MODIFY | Initialize new timeout defaults in constructor |
| [ConfigParser.cpp](file:///home/anbaya/Desktop/Webserv/srcs/config/ConfigParser.cpp) | MODIFY | Parse `cgi_idle_timeout` and `cgi_max_timeout` directives |
| [EventLoop.cpp](file:///home/anbaya/Desktop/Webserv/srcs/server/EventLoop.cpp) | MODIFY | Pass config timeouts to `startFromRequest()` instead of hardcoded `CGI_TIMEOUT_SEC` |
| [CGIResponseParser.hpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIResponseParser.hpp) | NO CHANGE | Reused by `_tryParseHeaders()` |
| [CGIResponseParser.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/CGIResponseParser.cpp) | NO CHANGE | Reused by `_tryParseHeaders()` |
| [test_cgi_runner.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/test_cgi_runner.cpp) | NO CHANGE | Uses `run()` with defaults, still works |

---

## Verification Plan

### Automated Tests

```bash
make cgi-test
```

All existing tests in [test_cgi_runner.cpp](file:///home/anbaya/Desktop/Webserv/srcs/cgi/test_cgi_runner.cpp) must still pass (backward compatibility via `_output` + default timeouts).

### Manual Verification

1. **Build** the project (`make`) — verify no compilation errors.
2. **Unit test the new API** — write a small test that:
   - Starts a CGI script that prints headers + body in chunks with `sleep()` between them.
   - Calls `onStdoutReady()` multiple times.
   - Verifies `headersReady()` becomes true after the first chunk containing `\r\n\r\n`.
   - Verifies `popOutput()` returns body chunks incrementally.
3. **Timeout tests**:
   - Script that sleeps 35s without output → killed by inactivity timeout (30s).
   - Script that outputs every 10s for 5 minutes + `cgi_max_timeout 60` → killed at 60s by absolute timeout.
   - Script that outputs every 5s indefinitely + no `cgi_max_timeout` → stays alive (inactivity never triggers).
4. **Integration** — once Person A wires up the new API in EventLoop, test with:
   ```bash
   curl -N http://localhost:8080/cgi-bin/slow_stream.py
   ```
   Verify that output appears progressively, not all at once.

## Open Questions

> [!IMPORTANT]
> **For Person A**: The `_removeEpollFd(clientFd)` at [EventLoop.cpp:L263](file:///home/anbaya/Desktop/Webserv/srcs/server/EventLoop.cpp#L263) pauses client write monitoring during CGI execution. Do you want to keep monitoring `clientFd` for `EPOLLERR|EPOLLHUP` (to detect client disconnect mid-CGI) even before streaming starts, or only re-register it when `CGI_HEADERS_READY` fires?

> [!IMPORTANT]
> **For Person B**: Should the `Response` class gain a new builder method like `buildFromCgiHeaders(const std::map<...>& headers)` that sets status + headers without a body and uses `Transfer-Encoding: chunked`? Or do you prefer Person A constructs the raw HTTP headers string directly?
