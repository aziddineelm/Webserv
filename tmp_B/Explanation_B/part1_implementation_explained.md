# Part 1 — HTTP Request Parsing: Full Implementation Walkthrough

---

## 0. The Big Picture — Why This Architecture?

### The core constraint: non-blocking I/O

Person A runs the event loop using `poll()`. Sockets are **non-blocking**, which means when `recv()` is called, it returns **whatever bytes are available right now** — which could be:

- 50 bytes of a 200-byte request line (partial)
- A complete GET request + the first half of the next one (pipelining)
- A 4096-byte chunk of a 5 MB POST body

This is fundamental. **You cannot write a parser that says "read the whole request then parse it"** because:
1. A large body (e.g. 5 MB) physically cannot arrive in one `recv()` call — the kernel socket buffer is typically 64–128 KB.
2. Waiting for it would block the entire server for all other clients.

### The solution: an Incremental State Machine

The parser holds its progress in a `ParseState` enum and an internal `_buffer`. Every time Person A calls `feed()`, the parser picks up exactly where it left off, consuming only what's available and stopping the moment it needs more data.

```cpp
// request.hpp — the 6 possible states
enum ParseState {
    REQUEST_LINE,   // initial — waiting for "GET /path HTTP/1.1\r\n"
    HEADERS,        // waiting for all "Key: Value\r\n" lines + blank line
    BODY,           // waiting for Content-Length bytes
    CHUNKED_BODY,   // waiting for hex-chunked segments
    COMPLETE,       // fully parsed — Person A may now use getters
    ERROR           // malformed request — Person A must send 4xx response
};
```

---

## 1. Data Storage — What Lives Inside the Class

```cpp
// request.hpp — private members
std::string  _method;        // "GET", "POST", "DELETE"
std::string  _uri;           // "/path?q=val"
std::string  _path;          // "/path"
std::string  _queryString;   // "q=val"
std::string  _version;       // "HTTP/1.1"
std::map<std::string, std::string> _headers; // lowercase key → value
std::string  _body;          // reassembled body content

ParseState   _state;         // current position in the state machine
std::string  _buffer;        // raw unprocessed bytes from recv()
size_t       _contentLength; // parsed from Content-Length header
bool         _isChunked;     // true if Transfer-Encoding: chunked
int          _errorCode;     // 400, 414, 431 — 0 means no error
```

**Key design point:** `_buffer` is the "inbox". Every call to `feed()` appends to it. Each parser step consumes from the front of `_buffer` as it processes data. When the request is complete, `_buffer` may still contain bytes — those belong to the **next** request (pipelining), so `reset()` deliberately **does NOT clear `_buffer`**.

---

## 2. The Entry Point — `feed()`

**File:** `request.cpp` lines 11–37

```cpp
void Request::feed(const std::string& data) {
    if (_state == COMPLETE || _state == ERROR)
        return;                          // (A) Guard: ignore data after done

    _buffer.append(data);               // (B) Accumulate raw bytes

    ParseState prevState;
    size_t prevBufSize;
    do {
        prevState   = _state;
        prevBufSize = _buffer.size();

        if (_state == REQUEST_LINE)  _parseRequestLine();
        if (_state == HEADERS)       _parseHeaders();
        if (_state == BODY)          _parseBody();
        if (_state == CHUNKED_BODY)  _parseChunkedBody();

    } while ((_state != prevState || _buffer.size() != prevBufSize)
              && _state != COMPLETE && _state != ERROR);
}
```

### (A) — The guard at lines 12–13

If the parser already reached `COMPLETE` or `ERROR`, any further `feed()` call is silently discarded. This prevents corrupting an already-finished request in a keep-alive scenario where Person A feeds data before checking `isComplete()`.

### (B) — Accumulation at line 15

`_buffer.append(data)` is **not** parsing — it's just stockpiling bytes. The buffer is the single source of truth. All parsing methods read from `_buffer` and erase what they consume from the front.

### (C) — The `do-while` loop at lines 20–36

This is the engine of the state machine. The loop runs the appropriate parser for the current state, then checks: **did anything change?**

Two conditions can signal progress:
1. `_state != prevState` — the state advanced (e.g. `REQUEST_LINE → HEADERS`)
2. `_buffer.size() != prevBufSize` — some bytes were consumed **without** changing state (e.g. `_skipLeadingCRLF()` strips `\r\n` but stays in `REQUEST_LINE`)

Without tracking `prevBufSize`, the CRLF-skip fix would cause an infinite loop because the state stays the same even though the buffer shrank. The loop stops when:
- Nothing changed (need more data from `recv()`)
- State is `COMPLETE` or `ERROR` (done)

Using `if` instead of `else if` for the state checks means that **within a single call to `feed()`**, the parser can chain through all states. For example, a complete GET request (request line + headers + no body) transitions `REQUEST_LINE → HEADERS → COMPLETE` all in one `feed()` call.

---

## 3. Step 1 — `_parseRequestLine()`

**File:** `request.cpp` lines 278–311

### Sub-step 1a: Skip leading CRLFs — `_skipLeadingCRLF()`

```cpp
// Lines 126–132
bool Request::_skipLeadingCRLF() {
    if (_buffer.size() >= 2 && _buffer[0] == '\r' && _buffer[1] == '\n') {
        _buffer.erase(0, 2);
        return true;
    }
    return false;
}
```

RFC 2616 §4.1 explicitly says: "In the interest of robustness, servers SHOULD ignore any empty line(s) received where a Request-Line is expected." Some HTTP clients send a bare `\r\n` after the previous request's body before the next request line. We strip it. Returning `true` signals to the caller that the buffer changed, so the `do-while` loop will retry.

### Sub-step 1b: Size check — Overflow Protection #1

```cpp
// Lines 284–292 in _parseRequestLine()
size_t pos = _buffer.find("\r\n");
if (pos == std::string::npos)
    return;              // No complete line yet — wait for more data

if (pos > 8192) {
    _setError(414);      // 414 URI Too Long
    return;
}
```

Before extracting the line, we check if the line (everything before `\r\n`) exceeds 8192 bytes. This protects against "header bomb" attacks where a client streams a gigantic URI to exhaust memory. If triggered, we set error code 414 and stop all processing immediately.

### Sub-step 1c: Extract the line — `_extractLine()`

```cpp
// Lines 137–144
bool Request::_extractLine(std::string& line) {
    size_t pos = _buffer.find("\r\n");
    if (pos == std::string::npos)
        return false;
    line = _buffer.substr(0, pos);   // copy text before \r\n
    _buffer.erase(0, pos + 2);       // consume line + \r\n from buffer
    return true;
}
```

This is one of the DRY helpers — the "find `\r\n`, extract, erase" pattern was repeated 3 times in the original code, so it was extracted into a single function following SRP.

### Sub-step 1d: Validate and split — `_splitRequestLine()`

```cpp
// Lines 148–170
bool Request::_splitRequestLine(const std::string& line) {
    std::istringstream iss(line);
    std::string method, uri, version, extra;
    iss >> method >> uri >> version;

    // Must be EXACTLY 3 tokens — no more, no less
    if (method.empty() || uri.empty() || version.empty() || (iss >> extra)) {
        _setError(400); return false;
    }
    // Only these 3 methods are accepted (per Webserv subject)
    if (method != "GET" && method != "POST" && method != "DELETE") {
        _setError(400); return false;
    }
    // Only these versions
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        _setError(400); return false;
    }
    _method = method;
    _uri    = uri;
    _version = version;
    return true;
}
```

`std::istringstream` is a clean C++98 way to tokenize. The `iss >> extra` trick catches lines like `"GET / HTTP/1.1 junk"` which have a 4th token — those are immediately rejected with 400.

### Sub-step 1e: Split URI — `_splitUri()`

```cpp
// Lines 173–182
void Request::_splitUri() {
    size_t qpos = _uri.find('?');
    if (qpos != std::string::npos) {
        _path        = _uri.substr(0, qpos);
        _queryString = _uri.substr(qpos + 1);
    } else {
        _path = _uri;
        _queryString.clear();
    }
}
```

`/search?q=hello` → `_path = "/search"`, `_queryString = "q=hello"`. Simple `find('?')` split. If no `?` exists, `_path` is the full URI and `_queryString` is empty.

### Final path validation

```cpp
// Lines 305–309
if (_path.empty() || _path[0] != '/') {
    _setError(400);
    return;
}
_state = HEADERS;   // ← advance the state machine
```

A valid HTTP path must start with `/`. After all checks pass, `_state` is advanced to `HEADERS`. The `do-while` loop in `feed()` will then immediately call `_parseHeaders()`.

---

## 4. Step 2 — `_parseHeaders()`

**File:** `request.cpp` lines 314–349

### Sub-step 2a: Extract header block — `_extractHeaderBlock()`

```cpp
// Lines 187–201
bool Request::_extractHeaderBlock(std::string& headerBlock) {
    size_t pos = _buffer.find("\r\n\r\n");
    if (pos != std::string::npos) {
        headerBlock = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 4);   // consume headers + blank line
        return true;
    }
    // Edge case: no headers at all (HTTP/1.0 "GET / HTTP/1.0\r\n\r\n")
    if (_buffer.size() >= 2 && _buffer[0] == '\r' && _buffer[1] == '\n') {
        headerBlock.clear();
        _buffer.erase(0, 2);
        return true;
    }
    return false;   // incomplete — wait for more data
}
```

HTTP headers end with the double blank line `\r\n\r\n`. We search for this sentinel. If it's not in the buffer yet, we return `false` and wait. The edge case handles HTTP/1.0 requests that send zero headers — after the request line, the buffer starts directly with `\r\n` (the blank line).

### Sub-step 2b: Overflow protection #2 — 431

```cpp
// Lines 321–324 in _parseHeaders()
if (headerBlock.size() > 16384) {
    _setError(431);   // 431 Request Header Fields Too Large
    return;
}
```

A 16 KB cap on the total header block prevents header bomb attacks. RFC 6585 standardized 431 for exactly this.

### Sub-step 2c: Parse each line — `_parseHeaderLine()`

```cpp
// Lines 205–223
bool Request::_parseHeaderLine(const std::string& line) {
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos) {
        _setError(400); return false;
    }
    // RFC 7230: no whitespace before the colon ("Host : val" is invalid)
    if (colonPos > 0 && std::isspace(static_cast<unsigned char>(line[colonPos - 1]))) {
        _setError(400); return false;
    }
    std::string key   = _toLower(_trim(line.substr(0, colonPos)));
    std::string value = _trim(line.substr(colonPos + 1));
    if (key.empty()) {
        _setError(400); return false;
    }
    _headers[key] = value;
    return true;
}
```

Three validations per line:
1. Must contain `:` — otherwise it's malformed.
2. No space before `:` — `"Host : val"` is rejected (RFC 7230). The `static_cast<unsigned char>` before `std::isspace` is critical: `char` can be signed, and passing a negative value to `isspace` is Undefined Behavior.
3. Key must not be empty after trimming.

Keys are stored **lowercase** via `_toLower()`. This means `getHeader("Content-Type")` and `getHeader("content-type")` both work correctly.

### Sub-step 2d: Validate headers — `_validateHeaders()`

```cpp
// Lines 227–258
void Request::_validateHeaders() {
    // HTTP/1.1 REQUIRES Host header (RFC 7230 §5.4)
    if (_version == "HTTP/1.1") {
        std::map<std::string, std::string>::const_iterator it = _headers.find("host");
        if (it == _headers.end() || it->second.empty()) {
            _setError(400); return;
        }
    }

    // Parse Content-Length as a base-10 number
    std::map<std::string, std::string>::const_iterator clIt = _headers.find("content-length");
    if (clIt != _headers.end()) {
        long cl;
        if (!_parseNumber(clIt->second, cl, 10)) {
            _setError(400); return;
        }
        _contentLength = static_cast<size_t>(cl);
    }

    // Detect chunked transfer encoding
    std::map<std::string, std::string>::const_iterator teIt = _headers.find("transfer-encoding");
    if (teIt != _headers.end()) {
        if (_toLower(teIt->second).find("chunked") != std::string::npos)
            _isChunked = true;
    }

    // RFC 2616 §4.4: chunked OVERRIDES Content-Length
    if (_isChunked)
        _contentLength = 0;
}
```

The DRY helper `_parseNumber()` consolidates all `strtol` calls:

```cpp
// Lines 109–118
bool Request::_parseNumber(const std::string& str, long& result, int base) {
    std::string trimmed = _trim(str);
    if (trimmed.empty()) return false;
    char* endPtr = NULL;
    result = std::strtol(trimmed.c_str(), &endPtr, base);
    if (endPtr == trimmed.c_str() || *endPtr != '\0' || result < 0)
        return false;
    return true;
}
```

`strtol` with `endPtr` validation catches: empty strings, strings with non-numeric characters (e.g. `"abc"`), and negative values. It's used for both `Content-Length` (base 10) and chunk sizes (base 16).

### Sub-step 2e: Decide next state — `_decideBodyState()`

```cpp
// Lines 261–271
void Request::_decideBodyState() {
    bool hasContentLength = (_headers.find("content-length") != _headers.end());

    if (_isChunked) {
        _state = CHUNKED_BODY;
    } else if (hasContentLength && _contentLength > 0) {
        _state = BODY;
    } else {
        _state = COMPLETE;   // GET with no body → done immediately
    }
}
```

Three outcomes:
- `Transfer-Encoding: chunked` → `CHUNKED_BODY`
- `Content-Length: N` (N > 0) → `BODY`
- Everything else (GET, DELETE, POST with no body) → `COMPLETE`

---

## 5. Step 3 — `_parseBody()` (Content-Length bodies)

**File:** `request.cpp` lines 352–358

```cpp
void Request::_parseBody() {
    if (_buffer.size() >= _contentLength) {
        _body = _buffer.substr(0, _contentLength);
        _buffer.erase(0, _contentLength);
        _state = COMPLETE;
    }
    // else: not enough bytes yet — return and wait for more feed() calls
}
```

This is intentionally simple. `_contentLength` was validated during header parsing. We just wait until the buffer has accumulated at least that many bytes, then extract exactly that many, leaving any remaining bytes in `_buffer` (they belong to the next pipelined request).

---

## 6. Step 4 — `_parseChunkedBody()` (Chunked Transfer Encoding)

**File:** `request.cpp` lines 361–397

Chunked encoding sends the body as a series of segments:
```
5\r\n          ← chunk size in hex
Hello\r\n      ← chunk data
6\r\n
 World\r\n
0\r\n          ← last chunk (size 0)
\r\n           ← trailing blank line
```

```cpp
void Request::_parseChunkedBody() {
    while (true) {
        // 1. Find the chunk size line
        size_t pos = _buffer.find("\r\n");
        if (pos == std::string::npos)
            return;   // no complete size line yet

        // 2. Parse hex size (strip chunk extensions like ";ext=val")
        std::string sizeStr = _buffer.substr(0, pos);
        size_t semiPos = sizeStr.find(';');
        if (semiPos != std::string::npos)
            sizeStr = sizeStr.substr(0, semiPos);

        long chunkSize;
        if (!_parseNumber(sizeStr, chunkSize, 16)) {
            _setError(400); return;   // not valid hex
        }

        // 3. Last chunk (size == 0) → done
        if (chunkSize == 0) {
            if (_buffer.size() < pos + 4)   // need "0\r\n\r\n"
                return;
            _buffer.erase(0, pos + 4);
            _state = COMPLETE;
            return;
        }

        // 4. Wait for: size_line\r\n + data + \r\n
        size_t totalNeeded = pos + 2 + static_cast<size_t>(chunkSize) + 2;
        if (_buffer.size() < totalNeeded)
            return;   // chunk data not fully arrived yet

        // 5. Extract chunk data, append to body
        _body.append(_buffer, pos + 2, static_cast<size_t>(chunkSize));
        _buffer.erase(0, totalNeeded);
        // loop continues to next chunk
    }
}
```

The `while(true)` loop processes **as many complete chunks as are available** in the buffer in a single `feed()` call. If at any point the buffer doesn't have a complete chunk yet, it returns immediately. The next `feed()` call will have more bytes and the loop will continue from the beginning (safely re-reading the size line that wasn't consumed).

---

## 7. Error Handling — `_setError()` and the Fast-Fail Design

```cpp
// Lines 87–90
void Request::_setError(int code) {
    _state = ERROR;
    _errorCode = code;
}
```

The moment any validation fails, `_setError()` is called. This sets the state to `ERROR` and stores the HTTP status code. After any private method calls `_setError()`, it returns immediately. The `do-while` in `feed()` checks `_state != ERROR` and stops. No further parsing ever happens.

**Why fast-fail?** Two reasons:
1. **Security:** If headers are malformed, `Content-Length` and `Transfer-Encoding` cannot be trusted. Continuing to read could enable request smuggling attacks.
2. **Performance:** No point parsing a 5 MB body for a request that's already invalid.

Person A's loop becomes:
```cpp
request.feed(chunk);
if (request.hasError())   // → send 4xx response immediately
if (request.isComplete()) // → route and respond
// else: keep reading from socket
```

---

## 8. Reset — Supporting Keep-Alive

**File:** `request.cpp` lines 49–61

```cpp
void Request::reset() {
    _method.clear();
    _uri.clear();
    _path.clear();
    _queryString.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
    _state       = REQUEST_LINE;
    _contentLength = 0;
    _isChunked   = false;
    _errorCode   = 0;
    // NOTE: _buffer is NOT cleared
}
```

`_buffer` is intentionally left untouched. After one request completes, `_buffer` may already contain the start bytes of the next request (HTTP pipelining). Clearing `_buffer` would lose those bytes. `reset()` clears only the parsed result fields, returning the parser to its initial state ready to parse the next request from whatever bytes remain.

---

## 9. SRP & DRY — The Refactoring Decisions

### Single Responsibility Principle

The original `_parseRequestLine()` was monolithic. After refactoring, each private function has exactly one job:

| Method | Single Responsibility |
|---|---|
| `_skipLeadingCRLF()` | Consume RFC-allowed blank lines before request |
| `_extractLine()` | Find `\r\n`, copy text, erase from buffer |
| `_splitRequestLine()` | Validate exactly 3 tokens, check method/version |
| `_splitUri()` | Split `_uri` at `?` into `_path` + `_queryString` |
| `_extractHeaderBlock()` | Detect `\r\n\r\n`, extract header text block |
| `_parseHeaderLine()` | Parse one `Key: Value` line into `_headers` |
| `_validateHeaders()` | Check Host, parse Content-Length, detect chunked |
| `_decideBodyState()` | Set next state: BODY / CHUNKED_BODY / COMPLETE |

### Don't Repeat Yourself

Two patterns were repeated in the original and extracted:
1. `strtol` with full validation → `_parseNumber(str, result, base)` — used by both `_validateHeaders()` (base 10) and `_parseChunkedBody()` (base 16)
2. `_buffer.find("\r\n")` + substr + erase → `_extractLine()` — reused wherever a single line needs to be consumed

---

## 10. Complete Data Flow — One Full Example

**Scenario:** `recv()` returns `"POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nHello"` in **two chunks**: first `"POST /upload HTTP/1.1\r\nHost: localhost\r\n"`, then `"Content-Length: 5\r\n\r\nHello"`.

```
CALL 1: feed("POST /upload HTTP/1.1\r\nHost: localhost\r\n")
  _buffer = "POST /upload HTTP/1.1\r\nHost: localhost\r\n"
  Loop iteration 1:
    state=REQUEST_LINE → _parseRequestLine()
      _skipLeadingCRLF() → false (no leading CRLF)
      find("\r\n") at pos=21 — within limit
      _extractLine() → line="POST /upload HTTP/1.1", _buffer="Host: localhost\r\n"
      _splitRequestLine() → _method="POST", _uri="/upload", _version="HTTP/1.1"
      _splitUri() → _path="/upload", _queryString=""
      _state = HEADERS ✓
  Loop iteration 2:
    state=HEADERS → _parseHeaders()
      _extractHeaderBlock() → find("\r\n\r\n") → not found
                             → buffer[0..1] != "\r\n"
                             → return false (wait for more data)
  Loop stops: state changed (REQUEST_LINE→HEADERS) but _parseHeaders returned without changing buffer
  feed() returns.

CALL 2: feed("Content-Length: 5\r\n\r\nHello")
  _buffer = "Host: localhost\r\nContent-Length: 5\r\n\r\nHello"
  Loop iteration 1:
    state=HEADERS → _parseHeaders()
      _extractHeaderBlock() → finds "\r\n\r\n" at pos=35
        headerBlock = "Host: localhost\r\nContent-Length: 5"
        _buffer = "Hello"
      headerBlock.size()=35 < 16384 ✓
      Parse "Host: localhost" → _headers["host"]="localhost"
      Parse "Content-Length: 5" → _headers["content-length"]="5"
      _validateHeaders():
        Host present ✓
        _parseNumber("5", cl, 10) → _contentLength=5
        no Transfer-Encoding
      _decideBodyState(): hasContentLength && _contentLength>0 → _state=BODY
  Loop iteration 2:
    state=BODY → _parseBody()
      _buffer.size()=5 >= _contentLength=5 ✓
      _body = "Hello"
      _buffer = ""
      _state = COMPLETE
  Loop stops: state=COMPLETE
  feed() returns.

Person A checks:
  request.isComplete() → true
  request.getMethod()  → "POST"
  request.getPath()    → "/upload"
  request.getBody()    → "Hello"
```

This is exactly how the code handles real-world incremental network data.
