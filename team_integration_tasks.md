# CGI Streaming Integration Tasks (For Person A & B)

## Overview
Person C has completed the CGI streaming engine. The `CGIHandler` no longer buffers the entire output in memory. Instead, it parses headers on-the-fly and exposes body chunks incrementally. 

To fully support streaming (e.g., Server-Sent Events, large downloads), the `EventLoop` (Person A) and `Response` (Person B) must be updated to use this new API.

---

## 👨‍💻 Person A: Event Loop Updates (`EventLoop.cpp`)

The EventLoop must change from waiting for `CGI_DONE` to streaming data while the CGI is in `STATE_CGI_RUNNING`.

### 1. Keep Client in `epoll`
Currently, `_spawnCgi()` calls `_removeEpollFd(clientFd);` to pause client write monitoring during CGI execution.
- **Action**: Remove or modify this. The client socket must remain in `epoll` (or be re-added when headers are ready) so that chunks can be sent to the client while the CGI process is still running.

### 2. New State: `STATE_CGI_STREAMING`
Add a new state to `ClientState` (in `Client.hpp`) called `STATE_CGI_STREAMING`.

### 3. Handle `headersReady()`
In `_handleCgiReady()`, detect when the CGI has emitted its headers:
```cpp
if (client.cgi.headersReady() && client.state == STATE_CGI_RUNNING) {
    // 1. Get parsed headers: const std::map<std::string, std::string>& headers = client.cgi.getCgiHeaders();
    // 2. Build HTTP Response headers (using Person B's new method)
    // 3. IMPORTANT: Set "Transfer-Encoding: chunked"
    // 4. Send HTTP headers to the client
    // 5. client.state = STATE_CGI_STREAMING;
}
```

### 4. Stream Body Chunks
Also in `_handleCgiReady()`, stream data as it becomes available:
```cpp
if (client.cgi.hasPendingOutput() && client.state == STATE_CGI_STREAMING) {
    std::string chunk = client.cgi.popOutput(); // default pops up to 8KB
    // 1. Format as chunked encoding: <hex_size>\r\n<chunk>\r\n
    // 2. Append to client.writeBuffer
}
```

### 5. Finalize the Stream
When the CGI finishes, send the terminating chunk:
```cpp
if (client.cgi.getState() == CGI_DONE && client.cgi.outputFullyConsumed()) {
    // 1. Append the final "0\r\n\r\n" chunk to client.writeBuffer
    // 2. Transition client back to STATE_READING (if keep-alive) or close
}
```

---

## 👨‍💻 Person B: HTTP Response Updates (`response.cpp`)

The `Response` class must be adapted so Person A can build a streaming HTTP response without having the body upfront.

### 1. New Builder Method
Currently, `buildFromCgiOutput()` parses an entire raw string.
- **Action**: Add a new method like `buildFromCgiHeaders(const std::map<std::string, std::string>& headers)`.
- **Logic**: This method should set `_statusCode` (extracting "Status" from the map if it exists, defaulting to 200) and copy the remaining map items into `_headers`.

### 2. Support Chunked Encoding
When a response is being streamed from CGI, we don't know the `Content-Length`.
- **Action**: Ensure the Response class can set `Transfer-Encoding: chunked`.
- **Action**: Make sure `Content-Length` is **not** automatically injected if chunked encoding is enabled.

### 3. Chunk Formatting Helper (Optional but Recommended)
To make Person A's life easier, add a static helper to format chunks:
```cpp
// Returns "<hex_size>\r\n<data>\r\n"
static std::string Response::formatChunk(const std::string& data);
```

---

## 🛠️ The New CGI API Available (From Person C)

Here is a quick reference of the new methods Person C added to `CGIHandler`:

- `bool headersReady() const;` — True when `\r\n\r\n` has been detected.
- `const std::map<std::string, std::string>& getCgiHeaders() const;` — Gets the parsed headers.
- `bool hasPendingOutput() const;` — True if there are body bytes waiting in the queue.
- `std::string popOutput(size_t maxBytes = 8192);` — Extracts the next body chunk.
- `bool outputFullyConsumed() const;` — True when stdout is closed AND the output queue is completely empty.
