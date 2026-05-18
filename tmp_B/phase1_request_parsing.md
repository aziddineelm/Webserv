# Phase 1 — Request Parsing: Implementation Roadmap

## What You're Building

A `Request` class that takes a raw `std::string` buffer and turns it into a structured object with method, URI, version, headers, and body — handling partial data, chunked encoding, and malformed input.

---

## Concepts You Must Understand Before Coding

### 1. HTTP Request Wire Format

Every HTTP request is plain ASCII text with this exact structure:

```
METHOD SP URI SP VERSION CRLF       ← Request line
Header-Name: Header-Value CRLF     ← Header (repeated)
Header-Name: Header-Value CRLF     ← Header (repeated)
CRLF                                ← Empty line = end of headers
body bytes...                       ← Optional body
```

Where:
- `SP` = single space character (` `)
- `CRLF` = `\r\n` (carriage return + line feed, 2 bytes)
- The empty `CRLF` between headers and body is the critical separator

**Real example on the wire:**
```
GET /index.html HTTP/1.1\r\n
Host: localhost:8080\r\n
Connection: keep-alive\r\n
\r\n
```

### 2. The Three Parts You Parse (In Order)

```
┌──────────────────────────────────────────────────┐
│ PART 1: Request Line                             │
│ "GET /index.html?page=2 HTTP/1.1"                │
│  ↓       ↓                ↓                      │
│ method   URI             version                 │
├──────────────────────────────────────────────────┤
│ PART 2: Headers                                  │
│ "Host: localhost:8080"                           │
│ "Content-Type: text/html"                        │
│ "Content-Length: 42"                              │
│ → stored in std::map<string, string>             │
├──────────────────────────────────────────────────┤
│ PART 3: Body (only if Content-Length > 0          │
│         or Transfer-Encoding: chunked)           │
│ "{"key":"value"}"                                │
│ → stored in std::string                          │
└──────────────────────────────────────────────────┘
```

### 3. The Incremental Parsing Problem

Person A gives you data in **arbitrary chunks**. You will NOT receive a complete request at once. Your parser must handle:

```
Feed #1: "GET /index.ht"           → incomplete request line, wait
Feed #2: "ml HTTP/1.1\r\nHo"      → request line done, header incomplete, wait
Feed #3: "st: localhost\r\n\r\n"   → headers done, no body → COMPLETE!
```

This means you need a **state machine** — a variable tracking what stage of parsing you're in.

### 4. The Parser States

```
PARSING_REQUEST_LINE
        │ (found first \r\n)
        ▼
PARSING_HEADERS
        │ (found \r\n\r\n)
        ▼
PARSING_BODY (if Content-Length > 0 or chunked)
        │ (read all body bytes)
        ▼
COMPLETE
```

If anything is malformed at any stage → state becomes `ERROR`.

### 5. Chunked Transfer Encoding

When the header says `Transfer-Encoding: chunked`, the body is NOT a flat block. Instead:

```
4\r\n        ← chunk size in hex (4 bytes)
Wiki\r\n     ← chunk data (exactly 4 bytes)
5\r\n        ← next chunk size (5 bytes)
pedia\r\n    ← chunk data (exactly 5 bytes)
0\r\n        ← chunk size 0 = last chunk
\r\n         ← final CRLF
```

You read chunks until you see size `0`. Then you reassemble them into a single body string: `"Wikipedia"`.

### 6. What Makes a Request Invalid (→ 400 Bad Request)

- Request line doesn't have exactly 3 parts
- Method is not `GET`, `POST`, or `DELETE`
- Version is not `HTTP/1.1` (or `HTTP/1.0`)
- Missing `Host` header (mandatory in HTTP/1.1)
- `Content-Length` is negative or not a number
- Malformed header (no colon separator)
- Chunked data has invalid hex size
- Both `Content-Length` and `Transfer-Encoding: chunked` are present (technically `chunked` wins, but you can reject this)

---

## Implementation Steps (In Order)

### Step 1: Define the Request class members

**File: `request.hpp`**

Your class needs to store:
```
Private members:
  - _method         (std::string)     "GET", "POST", "DELETE"
  - _uri            (std::string)     "/index.html?page=2"
  - _path           (std::string)     "/index.html"  (URI without query)
  - _queryString    (std::string)     "page=2"       (after the ?)
  - _version        (std::string)     "HTTP/1.1"
  - _headers        (std::map<std::string, std::string>)
  - _body           (std::string)
  - _state          (enum: REQUEST_LINE, HEADERS, BODY, CHUNKED_BODY, COMPLETE, ERROR)
  - _rawBuffer      (std::string)     accumulates raw data from Person A
  - _contentLength  (size_t)          parsed from Content-Length header
  - _isChunked      (bool)            true if Transfer-Encoding: chunked
  - _errorCode      (int)             400 if malformed, 0 if ok

Public methods:
  - feed(const std::string& data)     append data and try to parse
  - isComplete() const                true if state == COMPLETE
  - hasError() const                  true if state == ERROR
  - getMethod() const                 returns _method
  - getUri() const                    returns _uri
  - getPath() const                   returns _path
  - getQueryString() const            returns _queryString
  - getVersion() const                returns _version
  - getHeader(const std::string& key) returns header value or ""
  - getHeaders() const                returns all headers
  - getBody() const                   returns _body
  - getErrorCode() const              returns _errorCode
  - reset()                           clear everything for keep-alive reuse
```

Don't forget **Orthodox Canonical Form**: default constructor, copy constructor, assignment operator, destructor.

### Step 2: Implement `feed()` and request line parsing

This is the core method. Every time Person A calls `feed()`:

```
feed(data):
    _rawBuffer.append(data)
    
    if state == REQUEST_LINE:
        tryParseRequestLine()
    if state == HEADERS:
        tryParseHeaders()
    if state == BODY:
        tryParseBody()
    if state == CHUNKED_BODY:
        tryParseChunkedBody()
```

**`tryParseRequestLine()`:**
```
1. Look for "\r\n" in _rawBuffer
2. If not found → return (wait for more data)
3. Extract everything before "\r\n" → that's the request line
4. Split by spaces → must get exactly 3 parts: method, uri, version
5. Validate method is GET/POST/DELETE
6. Validate version is HTTP/1.1 or HTTP/1.0
7. Split uri on "?" → _path = before "?", _queryString = after "?"
8. Remove the consumed line from _rawBuffer (including the \r\n)
9. Set state = HEADERS
```

### Step 3: Implement header parsing

**`tryParseHeaders()`:**
```
1. Look for "\r\n\r\n" in _rawBuffer
2. If not found → return (headers not yet complete)
3. Everything before "\r\n\r\n" = header block
4. Split header block by "\r\n" → array of header lines
5. For each line:
   a. Find the ":" separator
   b. If no ":" → ERROR (400)
   c. Key = everything before ":"  (trimmed)
   d. Value = everything after ": " (trimmed)
   e. Store in _headers map (key should be case-insensitive — lowercase it)
6. After parsing all headers:
   a. Check if "host" header exists → if not and version is 1.1, ERROR (400)
   b. Check for "content-length" → parse to _contentLength
   c. Check for "transfer-encoding" containing "chunked" → _isChunked = true
7. Remove consumed data from _rawBuffer (up to and including \r\n\r\n)
8. Decide next state:
   - If _isChunked → state = CHUNKED_BODY
   - If _contentLength > 0 → state = BODY
   - Otherwise → state = COMPLETE (no body, e.g., GET request)
```

### Step 4: Implement body parsing (Content-Length)

**`tryParseBody()`:**
```
1. If _rawBuffer.size() >= _contentLength:
   a. _body = _rawBuffer.substr(0, _contentLength)
   b. _rawBuffer.erase(0, _contentLength)
   c. state = COMPLETE
2. Else → return (wait for more data)
```

That's it. Simple — you know exactly how many bytes to read.

### Step 5: Implement chunked body parsing

**`tryParseChunkedBody()`:**
```
Loop:
  1. Look for "\r\n" in _rawBuffer
  2. If not found → return (wait for more data)
  3. Extract the hex size string before "\r\n"
  4. Convert hex string to integer → chunkSize
  5. If chunkSize == 0:
     a. Remove "0\r\n\r\n" from buffer
     b. state = COMPLETE
     c. return
  6. Check if _rawBuffer has enough data: size_line + chunkSize + "\r\n"
  7. If not enough → return (wait for more data)
  8. Extract chunkSize bytes of data → append to _body
  9. Remove the entire chunk (size + \r\n + data + \r\n) from _rawBuffer
  10. Go back to step 1 (next chunk)
```

### Step 6: Implement validation and error handling

At each parsing step, if something is wrong:
```
Set _state = ERROR
Set _errorCode = 400
Return immediately
```

**Validation checklist:**
- [ ] Request line has exactly 3 parts
- [ ] Method is GET, POST, or DELETE
- [ ] Version is HTTP/1.1 or HTTP/1.0
- [ ] Every header line has a `:` separator
- [ ] `Host` header is present (for HTTP/1.1)
- [ ] `Content-Length` is a valid positive number (if present)
- [ ] Chunked sizes are valid hexadecimal
- [ ] No contradiction between Content-Length and Transfer-Encoding

### Step 7: Implement `reset()`

For keep-alive connections:
```
Clear _method, _uri, _path, _queryString, _version
Clear _headers
Clear _body
Set _state = REQUEST_LINE
Set _contentLength = 0
Set _isChunked = false
Set _errorCode = 0
DO NOT clear _rawBuffer — it might contain the start of the next request!
```

### Step 8: Test with hardcoded strings

Before Person A is ready, test your parser yourself in `main.cpp`:

```cpp
// Test 1: Simple GET
Request req;
req.feed("GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
assert(req.isComplete());
assert(req.getMethod() == "GET");
assert(req.getPath() == "/index.html");

// Test 2: Partial data
Request req2;
req2.feed("GET /index");
assert(!req2.isComplete());
req2.feed(".html HTTP/1.1\r\nHost: local");
assert(!req2.isComplete());
req2.feed("host\r\n\r\n");
assert(req2.isComplete());

// Test 3: POST with body
Request req3;
req3.feed("POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello");
assert(req3.isComplete());
assert(req3.getBody() == "hello");

// Test 4: Malformed
Request req4;
req4.feed("INVALID REQUEST\r\n\r\n");
assert(req4.hasError());

// Test 5: Chunked
Request req5;
req5.feed("POST /data HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n");
req5.feed("5\r\nhello\r\n0\r\n\r\n");
assert(req5.isComplete());
assert(req5.getBody() == "hello");
```

### Step 9: Test with telnet (real network)

Once Person A has a basic socket listener:
```bash
telnet localhost 8080
```
Then type:
```
GET /index.html HTTP/1.1
Host: localhost

```
(Press Enter twice after Host to send the empty line)

---

## File Structure

```
srcs/http/request/
  ├── request.hpp        ← class definition + enum
  └── request.cpp        ← all implementation
```

---

## Priority Order (What to Code First)

| Order | What | Time | Why First |
|-------|------|------|-----------|
| 1 | Class skeleton with members + OCF | 30 min | Foundation |
| 2 | `feed()` + `tryParseRequestLine()` | 1-2 hours | Most basic — parse `GET /path HTTP/1.1` |
| 3 | `tryParseHeaders()` | 1-2 hours | Parse `Key: Value` pairs |
| 4 | `tryParseBody()` (Content-Length) | 30 min | Simple — just read N bytes |
| 5 | Validation + error state | 1 hour | Catch bad requests |
| 6 | `tryParseChunkedBody()` | 1-2 hours | Hardest part — hex parsing + reassembly |
| 7 | `reset()` for keep-alive | 15 min | Quick |
| 8 | Test with hardcoded strings | 1-2 hours | Verify everything works |
| **Total** | | **~6-10 hours** | |

---

## Common Mistakes to Avoid

| Mistake | Why It's Wrong | Fix |
|---------|---------------|-----|
| Clearing `_rawBuffer` in `reset()` | Leftover bytes belong to the next request (keep-alive) | Only clear parsed data, not the buffer |
| Using `\n` instead of `\r\n` | HTTP requires CRLF, not just LF | Always search for `\r\n` |
| Case-sensitive header lookup | `Host` and `host` and `HOST` are all the same | Lowercase the key before storing |
| Parsing before data is complete | You get "GET /ind" — that's not a full line yet | Always check for `\r\n` first |
| Forgetting to erase consumed data | Buffer grows forever, same data parsed twice | `_rawBuffer.erase(0, consumedBytes)` after each parse step |
| Not handling `Content-Length: 0` | POST with `Content-Length: 0` means empty body, not "no body" | If header exists with value 0, skip to COMPLETE |
