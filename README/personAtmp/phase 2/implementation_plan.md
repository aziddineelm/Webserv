# Phase 2 — Event Loop Implementation Plan (Person A)

Build a fully functional, non-blocking event loop that can accept clients, read data, write responses, and clean up idle connections — all driven by a single `poll()` call.

## Current State

**Task 1 is DONE.** The following already works:
- `poll()` loop running in [EventLoop.cpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/EventLoop.cpp)
- Listener FD registration via `addListenFd()`
- Accept queue draining (loop `accept()` until `EAGAIN`)
- Client IP logging (manual formatting — `inet_ntoa` forbidden)
- Poll array helpers: `_addPollFd`, `_removePollFd`, `_setPollEvents`
- [Client.hpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/Client.hpp) struct defined (`fd`, `state`, `readBuffer`, `writeBuffer`, `writeOffset`, `lastActivity`, `listenPort`)

**What's still stubbed:**
- `_handleAccept` closes the client FD immediately (line 145) instead of tracking it
- `_handleRead` — empty
- `_handleWrite` — empty
- `_handleDisconnect` — empty
- `_checkTimeouts` — empty

---

## Proposed Changes

### Task 2 — Client Tracking & Reading

> **Goal:** Accepted clients stay alive in the poll set. Incoming data accumulates in their `readBuffer`. When a complete HTTP request arrives (or for now, any data ending with `\r\n\r\n`), transition the client to `STATE_WRITING` with an echo response.

#### [MODIFY] [EventLoop.cpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/EventLoop.cpp)

**2a. `_handleAccept` — track accepted clients instead of closing them**

Replace the `close(clientFd)` at line 145 with:

```cpp
// Add to poll set (watch for POLLIN — client will send data)
_addPollFd(clientFd, POLLIN);

// Create Client entry
_clients[clientFd] = Client(clientFd, port);
```

This wires accepted FDs into the poll set so `poll()` will report when they have data ready.

**2b. `_handleRead` — non-blocking recv into readBuffer**

```cpp
void EventLoop::_handleRead(int clientFd) {
    std::map<int, Client>::iterator it = _clients.find(clientFd);
    if (it == _clients.end())
        return;
    Client &client = it->second;

    char buf[4096];
    while (true) {
        ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
        if (n > 0) {
            client.readBuffer.append(buf, n);
            client.lastActivity = time(NULL);
        } else if (n == 0) {
            // Client sent FIN — graceful disconnect
            _handleDisconnect(clientFd);
            return;
        } else {
            // n == -1: EAGAIN means no more data right now — normal
            break;
        }
    }

    // Check if we have a complete HTTP request (ends with \r\n\r\n)
    // This is a TEMPORARY check — Person B's parser will replace this
    if (client.readBuffer.find("\r\n\r\n") != std::string::npos) {
        // Build a simple echo response (Phase 2 test only)
        std::string body = "<html><body><h1>Webserv Echo</h1><pre>"
                           + client.readBuffer + "</pre></body></html>";
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/html\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;
        client.writeBuffer = oss.str();
        client.writeOffset = 0;
        client.state = STATE_WRITING;
        client.readBuffer.clear();

        // Now watch for POLLOUT so we can send the response
        _setPollEvents(clientFd, POLLIN | POLLOUT);
    }
}
```

**New include needed** at the top of `EventLoop.cpp`:
```cpp
#include <sstream>   // std::ostringstream
```

> [!IMPORTANT]
> The `recv()` loop drains all available data per `POLLIN` event (same pattern as the accept loop). We break on `EAGAIN`, not on the first successful read. The echo response is temporary — Person B's `Request` parser and `Response` builder will replace this in Phase 3.

---

### Task 3 — Writing (Partial Send Handling)

> **Goal:** Send the response from `writeBuffer`, handling partial sends. When fully sent, either close the connection or reset for keep-alive.

#### [MODIFY] [EventLoop.cpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/EventLoop.cpp)

```cpp
void EventLoop::_handleWrite(int clientFd) {
    std::map<int, Client>::iterator it = _clients.find(clientFd);
    if (it == _clients.end())
        return;
    Client &client = it->second;

    const char *data = client.writeBuffer.c_str() + client.writeOffset;
    size_t remaining = client.writeBuffer.size() - client.writeOffset;

    ssize_t n = send(clientFd, data, remaining, 0);
    if (n > 0) {
        client.writeOffset += n;
        client.lastActivity = time(NULL);

        // Check if entire response has been sent
        if (client.writeOffset >= client.writeBuffer.size()) {
            // For now: close after response (Connection: close)
            // Phase 3: check for keep-alive and reset instead
            client.state = STATE_DONE;
            _handleDisconnect(clientFd);
        }
    } else if (n == 0) {
        // Should not happen with send(), but handle defensively
        _handleDisconnect(clientFd);
    } else {
        // n == -1: EAGAIN means kernel buffer full — wait for next POLLOUT
        // Any other error → disconnect
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            _handleDisconnect(clientFd);
    }
}
```

> [!NOTE]
> We call `send()` **once** per `POLLOUT` event (not in a loop). This is intentional — if the kernel buffer is full, `poll()` will tell us when it's ready again. Looping could starve other clients.

---

### Task 4 — Disconnect & Cleanup

> **Goal:** One central place to close a client FD, remove it from the poll set, and erase it from the `_clients` map.

#### [MODIFY] [EventLoop.cpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/EventLoop.cpp)

```cpp
void EventLoop::_handleDisconnect(int clientFd) {
    std::cout << "[EventLoop] Client disconnected (fd " << clientFd << ")" << std::endl;

    close(clientFd);
    _removePollFd(clientFd);
    _clients.erase(clientFd);
}
```

**Why this order matters:**
1. `close()` first — releases the kernel FD
2. `_removePollFd()` — removes from poll set (swap-and-pop, already implemented)
3. `_clients.erase()` — removes the client data

> [!WARNING]
> **Safe iteration concern:** `_handleDisconnect` can be called during the event processing loop (from `_handleRead` on `recv()==0`, from `_handleWrite` on error, or from `POLLHUP`). After calling it, the pollfd array has changed via swap-and-pop. The current loop in `run()` iterates by index with a `ready` counter, so once we process this FD and decrement `ready`, we move on. However, if the swapped-in entry at this index also has `revents` set, it could be missed. This is acceptable for now — it will be caught on the next `poll()` cycle. If this becomes an issue, we can switch to deferred removal.

---

### Task 5 — Timeout Scanning

> **Goal:** Drop clients that have been idle for more than `CLIENT_TIMEOUT_SEC` (60s). This prevents resource exhaustion from abandoned connections.

#### [MODIFY] [EventLoop.cpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/EventLoop.cpp)

```cpp
void EventLoop::_checkTimeouts() {
    time_t now = time(NULL);

    // Collect FDs to close (can't modify _clients while iterating)
    std::vector<int> timedOut;

    for (std::map<int, Client>::iterator it = _clients.begin();
         it != _clients.end(); ++it) {
        if (now - it->second.lastActivity > CLIENT_TIMEOUT_SEC) {
            timedOut.push_back(it->first);
        }
    }

    for (size_t i = 0; i < timedOut.size(); ++i) {
        std::cout << "[EventLoop] Timeout: closing fd " << timedOut[i] << std::endl;
        _handleDisconnect(timedOut[i]);
    }
}
```

> [!NOTE]
> We collect FDs into a vector first, then disconnect them. This avoids modifying the `_clients` map while iterating over it — a classic invalidation bug.

---

## Files Changed Summary

| File | What Changes |
|------|-------------|
| [EventLoop.cpp](file:///home/aysadeq/Desktop/Webserv/srcs/server/EventLoop.cpp) | All 4 stubs filled in + `#include <sstream>` added + accept tracking replaces `close()` |

No new files. No header changes. No Makefile changes.

---

## Verification Plan

### Build Test
```bash
make re
# Must compile with c++ -Wall -Wextra -Werror -std=c++98
```

### Test 1 — Echo Response via curl
```bash
./webserv &
curl -v http://localhost:8080/
# Expected: HTTP 200 with echo of the raw request in <pre> tags
```

### Test 2 — Multiple Simultaneous Clients
```bash
# Terminal 1:
curl http://localhost:8080/ &
# Terminal 2:
curl http://localhost:8080/ &
# Terminal 3:
curl http://localhost:8081/ &
# All three should get responses
```

### Test 3 — Timeout (idle client)
```bash
telnet localhost 8080
# Connect but send nothing — wait 60+ seconds
# Server should log: "[EventLoop] Timeout: closing fd X"
```

### Test 4 — Graceful Disconnect
```bash
telnet localhost 8080
# Type partial data, then Ctrl+] → quit
# Server should log: "[EventLoop] Client disconnected (fd X)"
```

### Test 5 — No FD Leaks
```bash
./webserv &
PID=$!
ls /proc/$PID/fd | wc -l   # Note baseline
# Run 50 curl requests
for i in $(seq 1 50); do curl -s http://localhost:8080/ > /dev/null; done
ls /proc/$PID/fd | wc -l   # Should match baseline
```

### Test 6 — Partial Send (large response)
```bash
# Modify the echo response body to be very large (e.g., repeat a string 10000 times)
# Verify curl receives the complete response with no truncation
```

---

## Open Questions

> [!IMPORTANT]
> **POLLHUP + POLLIN ordering:** Currently in the event loop (line 84-89), if both `POLLIN` and `POLLHUP` are set, we read first then disconnect. This is correct behavior (there may be final data). However, if `_handleRead` itself calls `_handleDisconnect` (on `recv()==0`), then the `POLLHUP` check at line 88 will try to disconnect an already-closed FD. Should I add a guard in `_handleDisconnect` to check if the FD still exists in `_clients` before acting? **My recommendation: yes, add the guard.**

> [!NOTE]
> **Send loop vs single send:** I chose a single `send()` call per `POLLOUT` event to avoid starving other clients. An alternative is to loop `send()` until `EAGAIN`, which would drain the buffer faster but could delay other clients' events. For a 42 project with moderate load, single-send is simpler and safer. Thoughts?
