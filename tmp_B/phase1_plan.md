# Phase 1 — Request Parsing: Implementation Plan

## Scope: What Phase 1 IS and IS NOT

| Phase 1 IS | Phase 1 IS NOT |
|------------|----------------|
| Parsing raw bytes into a structured `Request` object | Building HTTP responses (Phase 2) |
| Handling partial/incremental data from Person A | Routing URIs to files (Phase 3) |
| Parsing request line, headers, body | Serving static files (Phase 3) |
| Chunked transfer encoding (un-chunking) | File uploads / multipart parsing (Phase 4) |
| Detecting malformed requests (→ error state) | Generating error page HTML (Phase 2) |
| Validating methods: GET, POST, DELETE | Virtual hosting logic (Phase 5) |

---

## Subject Requirements That Apply to Phase 1

From `en.Webserv.txt`:

| Requirement | What It Means for Your Parser |
|-------------|-------------------------------|
| "GET, POST, and DELETE methods" (line 198) | Parse and validate these 3 methods only |
| "HTTP response status codes must be accurate" (line 193) | Your parser must set the right error code (400) for bad requests |
| "server must remain non-blocking" (line 163) | Your parser must handle partial data — never assume the full request arrived |
| "for chunked requests, your server needs to un-chunk them" (line 275) | You must implement chunked body parsing and reassembly |
| "maximum allowed size for client request bodies" (line 243) | Check `Content-Length` against `client_max_body_size` (→ 413) |
| "compatible with standard web browsers" (line 189) | Must handle real browser requests (Chrome, Firefox) |
| "C++ 98 standard" (line 91) | No auto, no nullptr, no range-based for, OCF required |

---

## Class Design

### The Enum (Parser State Machine)

```cpp
enum ParseState {
    PARSE_REQUEST_LINE,  // waiting for "METHOD URI VERSION\r\n"
    PARSE_HEADERS,       // waiting for "\r\n\r\n"
    PARSE_BODY,          // reading Content-Length bytes
    PARSE_CHUNKED_BODY,  // reading chunked segments
    PARSE_COMPLETE,      // full request parsed
    PARSE_ERROR          // malformed request detected
};
```

### The Class

```
request.hpp
─────────────────────────────────────────────────────────

class Request {
public:
    // Orthodox Canonical Form
    Request();
    Request(const Request& other);
    Request& operator=(const Request& other);
    ~Request();

    // Core interface — Person A calls this
    void    feed(const char* data, size_t len);
    bool    isComplete() const;
    bool    hasError() const;
    void    reset();

    // Getters — Person B uses internally, later used by Router
    const std::string&  getMethod() const;
    const std::string&  getUri() const;
    const std::string&  getPath() const;
    const std::string&  getQueryString() const;
    const std::string&  getVersion() const;
    std::string         getHeader(const std::string& key) const;
    const std::map<std::string, std::string>&  getHeaders() const;
    const std::string&  getBody() const;
    int                 getErrorCode() const;

private:
    // Parsed data
    std::string     _method;        // "GET", "POST", "DELETE"
    std::string     _uri;           // "/path?query=val"
    std::string     _path;          // "/path"
    std::string     _queryString;   // "query=val"
    std::string     _version;       // "HTTP/1.1"
    std::map<std::string, std::string> _headers;
    std::string     _body;

    // Parser state
    ParseState      _state;
    std::string     _buffer;        // raw accumulated bytes
    size_t          _contentLength;
    bool            _isChunked;
    int             _errorCode;     // 0 = ok, 400 = bad request, 413 = too large

    // Private parsing methods
    void    _parseRequestLine();
    void    _parseHeaders();
    void    _parseBody();
    void    _parseChunkedBody();

    // Utility
    static std::string _toLower(const std::string& str);
    static std::string _trim(const std::string& str);
};
```

### Includes Needed

```cpp
#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>
#include <map>
#include <sstream>    // for string-to-int conversion (C++98)
#include <cstdlib>    // for strtol (hex parsing)

// ... class definition ...

#endif
```

---

## State Machine Diagram

```
                    feed() called
                        │
                        ▼
              ┌─────────────────┐
              │ PARSE_REQUEST   │ ◀─── initial state
              │     _LINE       │
              └────────┬────────┘
                       │ found \r\n → parsed method/uri/version
                       │ (invalid? → PARSE_ERROR)
                       ▼
              ┌─────────────────┐
              │ PARSE_HEADERS   │
              └────────┬────────┘
                       │ found \r\n\r\n → parsed all headers
                       │ (invalid? → PARSE_ERROR)
                       │
              ┌────────┼────────────────┐
              │        │                │
     (chunked)│  (content-length > 0)  │ (no body)
              ▼        ▼                ▼
     ┌────────────┐ ┌──────────┐ ┌──────────────┐
     │PARSE_CHUNK │ │PARSE_BODY│ │PARSE_COMPLETE│
     │  ED_BODY   │ │          │ │              │
     └─────┬──────┘ └────┬─────┘ └──────────────┘
           │              │
           │ size 0 chunk │ read all bytes
           ▼              ▼
     ┌──────────────────────┐
     │   PARSE_COMPLETE     │
     └──────────────────────┘

     At ANY point: malformed data → PARSE_ERROR
```

---

## Implementation Steps

### Step 1: File setup + class skeleton (30 min)

**What to do:**
1. Add include guard to `request.hpp`
2. Add all includes: `<string>`, `<map>`, `<sstream>`, `<cstdlib>`
3. Define the `ParseState` enum
4. Define the full class with all members listed above
5. In `request.cpp`: implement OCF (constructor initializes `_state = PARSE_REQUEST_LINE`, `_contentLength = 0`, `_isChunked = false`, `_errorCode = 0`)

**Done when:** It compiles with `c++ -std=c++98 -Wall -Wextra -Werror`.

---

### Step 2: `feed()` + `_parseRequestLine()` (1–2 hours)

**`feed()`** — the entry point Person A calls:
```
feed(data, len):
    append data to _buffer

    while (true):                    // loop because one feed() might
        if _state == REQUEST_LINE:   // advance through multiple states
            _parseRequestLine()
        else if _state == HEADERS:
            _parseHeaders()
        else if _state == BODY:
            _parseBody()
        else if _state == CHUNKED_BODY:
            _parseChunkedBody()
        else:
            break                    // COMPLETE or ERROR — stop

        if (state didn't change this iteration):
            break                    // need more data — stop
```

> [!IMPORTANT]
> The `while` loop + "break if state didn't change" pattern is critical. One `feed()` call might deliver the entire request at once — you must advance through ALL states in a single call, not wait for the next `feed()`.

**`_parseRequestLine()`:**
```
1. pos = _buffer.find("\r\n")
2. if pos == npos → return (need more data)
3. requestLine = _buffer.substr(0, pos)
4. _buffer.erase(0, pos + 2)              // +2 for \r\n

5. Split requestLine by spaces:
   - use std::istringstream → extract method, uri, version
   - if any is empty or there are extra tokens → ERROR 400

6. Validate:
   - method must be "GET" or "POST" or "DELETE"
   - version must be "HTTP/1.1" or "HTTP/1.0"
   - if fail → _state = PARSE_ERROR, _errorCode = 400, return

7. Split URI on '?':
   - qpos = _uri.find('?')
   - if found: _path = uri.substr(0, qpos), _queryString = uri.substr(qpos + 1)
   - if not found: _path = uri, _queryString = ""

8. _state = PARSE_HEADERS
```

**Done when:** `feed("GET /test HTTP/1.1\r\n...")` correctly fills `_method`, `_path`, `_version`.

---

### Step 3: `_parseHeaders()` (1–2 hours)

```
1. pos = _buffer.find("\r\n\r\n")
2. if pos == npos → return (headers not complete yet)
3. headerBlock = _buffer.substr(0, pos)
4. _buffer.erase(0, pos + 4)              // +4 for \r\n\r\n

5. Split headerBlock by "\r\n" → iterate each line:
   a. colonPos = line.find(':')
   b. if colonPos == npos → ERROR 400 (no colon = invalid header)
   c. key = toLower(trim(line.substr(0, colonPos)))
   d. value = trim(line.substr(colonPos + 1))
   e. _headers[key] = value

6. Post-header validation:
   a. if _version == "HTTP/1.1" and _headers has no "host" → ERROR 400
   b. if _headers has "content-length":
      - parse value to size_t (_contentLength)
      - if not a valid number → ERROR 400
   c. if _headers has "transfer-encoding" and value contains "chunked":
      - _isChunked = true

7. Decide next state:
   if _isChunked → _state = PARSE_CHUNKED_BODY
   else if _contentLength > 0 → _state = PARSE_BODY
   else → _state = PARSE_COMPLETE
```

**Done when:** `getHeader("host")` returns `"localhost"` after feeding a full GET request.

---

### Step 4: `_parseBody()` (30 min)

The simplest step:
```
1. if _buffer.size() >= _contentLength:
   a. _body = _buffer.substr(0, _contentLength)
   b. _buffer.erase(0, _contentLength)
   c. _state = PARSE_COMPLETE
2. else → return (need more data)
```

**Done when:** POST with `Content-Length: 5` and body `"hello"` → `getBody() == "hello"`.

---

### Step 5: `_parseChunkedBody()` (1–2 hours)

This is the trickiest part:
```
Loop:
  1. pos = _buffer.find("\r\n")
  2. if pos == npos → return (need more data)
  3. sizeStr = _buffer.substr(0, pos)
  4. chunkSize = strtol(sizeStr.c_str(), &endPtr, 16)  // hex to int
  5. if endPtr == sizeStr.c_str() → ERROR 400 (invalid hex)

  6. if chunkSize == 0:
     // Last chunk: expect "0\r\n\r\n"
     if _buffer.size() >= pos + 4:   // "0\r\n" + "\r\n"
       _buffer.erase(0, pos + 4)
       _state = PARSE_COMPLETE
       return
     else → return (need more data for trailing CRLF)

  7. totalNeeded = pos + 2 + chunkSize + 2
     // (size_line + \r\n + data + \r\n)
  8. if _buffer.size() < totalNeeded → return (need more data)

  9. chunkData = _buffer.substr(pos + 2, chunkSize)
  10. _body.append(chunkData)
  11. _buffer.erase(0, totalNeeded)
  12. continue loop (process next chunk)
```

> [!WARNING]
> From the subject (line 275): "for chunked requests, your server needs to un-chunk them, the CGI will expect EOF as the end of the body." This means after un-chunking, `_body` must contain the reassembled flat body — no chunk metadata.

**Done when:** Chunked body `"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"` → `getBody() == "hello world"`.

---

### Step 6: Validation & error handling (1 hour)

Go back through each parsing method and add all validations:

| Check | Where | Error |
|-------|-------|-------|
| Request line has ≠ 3 parts | `_parseRequestLine` | 400 |
| Method not GET/POST/DELETE | `_parseRequestLine` | 400 |
| Version not HTTP/1.0 or HTTP/1.1 | `_parseRequestLine` | 400 |
| Header line has no `:` | `_parseHeaders` | 400 |
| Missing `Host` (HTTP/1.1) | `_parseHeaders` | 400 |
| `Content-Length` not a number | `_parseHeaders` | 400 |
| `Content-Length` is negative | `_parseHeaders` | 400 |
| Invalid hex in chunk size | `_parseChunkedBody` | 400 |
| URI is empty | `_parseRequestLine` | 400 |

**Implementation pattern:**
```cpp
void Request::_setError(int code) {
    _state = PARSE_ERROR;
    _errorCode = code;
}
```
Call `_setError(400)` and `return` immediately whenever validation fails.

---

### Step 7: `reset()` for keep-alive (15 min)

```
Clear: _method, _uri, _path, _queryString, _version, _headers, _body
Reset: _state = PARSE_REQUEST_LINE
Reset: _contentLength = 0, _isChunked = false, _errorCode = 0
DO NOT CLEAR: _buffer  ← may contain bytes of the next request!
```

---

### Step 8: Utility methods (30 min)

**`_toLower()`** — for case-insensitive header keys:
```cpp
std::string Request::_toLower(const std::string& str) {
    std::string result = str;
    for (size_t i = 0; i < result.size(); i++)
        result[i] = std::tolower(result[i]);
    return result;
}
```

**`_trim()`** — remove leading/trailing whitespace:
```cpp
std::string Request::_trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t");
    return str.substr(start, end - start + 1);
}
```

---

## Test Plan

Write these in `main.cpp` or a separate test file. Use `assert()` for verification.

| # | Test | Input | Expected |
|---|------|-------|----------|
| 1 | Simple GET | `GET / HTTP/1.1\r\nHost: localhost\r\n\r\n` | complete, method=GET, path=/ |
| 2 | GET with query | `GET /search?q=hello HTTP/1.1\r\nHost: x\r\n\r\n` | path=/search, queryString=q=hello |
| 3 | POST with body | `POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc` | complete, body=abc |
| 4 | Partial request line | Feed `"GET /ind"` then `"ex HTTP/1.1\r\nHost: x\r\n\r\n"` | not complete after first, complete after second |
| 5 | Partial body | `POST ... Content-Length: 10\r\n\r\nhell` then `o12345` | complete after second feed, body=hello12345 |
| 6 | Chunked body | `POST ... Transfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n` | complete, body=abc |
| 7 | Bad method | `PUT / HTTP/1.1\r\nHost: x\r\n\r\n` | error, errorCode=400 |
| 8 | Missing Host | `GET / HTTP/1.1\r\n\r\n` | error, errorCode=400 |
| 9 | Bad request line | `GARBAGE\r\n\r\n` | error, errorCode=400 |
| 10 | Keep-alive reset | Feed complete GET → reset → feed another GET | second request parses correctly |

---

## Done Criteria

Phase 1 is **complete** when:

- [ ] Any valid GET/POST/DELETE request is parsed into method, path, query, version, headers, body
- [ ] Partial data is handled (multiple `feed()` calls build up the request)
- [ ] Chunked bodies are un-chunked into a flat `_body` string
- [ ] All 10 test cases pass
- [ ] Malformed requests set `_state = PARSE_ERROR` and `_errorCode = 400`
- [ ] `reset()` works for keep-alive without losing leftover buffer data
- [ ] Compiles with `c++ -std=c++98 -Wall -Wextra -Werror`
- [ ] No memory leaks (run with `valgrind` if possible)

---

## Pitfalls

| Mistake | Why It Hurts | Prevention |
|---------|-------------|------------|
| Clearing `_buffer` in `reset()` | Loses the start of the next keep-alive request | Only clear parsed fields |
| Searching for `\n` instead of `\r\n` | HTTP mandates CRLF — will break with real browsers | Always use `\r\n` |
| Case-sensitive header keys | `Host` vs `host` vs `HOST` are identical in HTTP | `_toLower()` before storing |
| Not looping in `feed()` | One feed might deliver the entire request — needs to advance through all states | `while` loop with "break if no progress" |
| Parsing before `\r\n` is found | Half a line looks valid but you're cutting mid-token | Always check for delimiter first |
| Forgetting to erase from `_buffer` | Buffer grows, same data re-parsed | `_buffer.erase(0, consumed)` in every parse step |
| Using `std::stoi` or `std::to_string` | C++11 — forbidden in this project | Use `std::istringstream` or `strtol` |
| Not handling `Content-Length: 0` | Valid POST with empty body — should still reach COMPLETE | Treat 0 as "body exists but is empty" |
