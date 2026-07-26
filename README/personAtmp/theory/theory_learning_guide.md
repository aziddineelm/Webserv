# Webserv — Theory & Learning Guide
## Shared Foundation → Person A (Core Server & Networking)

> [!NOTE]
> Every concept has **searchable terms** in bold. Copy-paste them into Google/YouTube for deep dives. Concepts are ordered so each one builds on the previous.

---

# PART 1: SHARED CONCEPTUAL FOUNDATION

> Everyone must understand these before splitting.

---

## 1. Computer Networking Fundamentals

### 1.1 The OSI Model (Open Systems Interconnection)
A 7-layer abstraction of how data travels across a network.

| Layer | Name | What It Does | Webserv Relevance |
|-------|------|-------------|-------------------|
| 7 | **Application Layer** | HTTP, FTP, DNS | ✅ You build here (HTTP server) |
| 6 | Presentation | Encoding, encryption | SSL/TLS (not required) |
| 5 | Session | Session management | Connection keep-alive |
| 4 | **Transport Layer** | TCP, UDP | ✅ Your server uses TCP |
| 3 | Network | IP addressing, routing | IP addresses |
| 2 | Data Link | Ethernet frames | Below your concern |
| 1 | Physical | Cables, signals | Below your concern |

**Search:** `OSI model explained`, `OSI model 7 layers`

### 1.2 TCP/IP Model (Internet Protocol Suite)
The practical 4-layer model the internet actually uses.

| Layer | Protocols | Your concern? |
|-------|-----------|--------------|
| **Application** | HTTP, DNS, FTP | ✅ Yes |
| **Transport** | TCP, UDP | ✅ Yes |
| **Internet** | IP (IPv4/IPv6) | Partially (addresses) |
| **Link** | Ethernet, Wi-Fi | No |

**Search:** `TCP/IP model`, `TCP/IP vs OSI model`

### 1.3 TCP (Transmission Control Protocol)
The transport protocol your server uses. Key properties:

| Property | Meaning |
|----------|---------|
| **Connection-oriented** | Must establish connection before sending data (3-way handshake) |
| **Reliable delivery** | Lost packets are retransmitted automatically |
| **Ordered** | Data arrives in the same order it was sent |
| **Byte-stream** | No message boundaries — just a continuous stream of bytes |
| **Flow control** | Sender slows down if receiver is overwhelmed |

**The TCP 3-Way Handshake:**
```
Client              Server
  │── SYN ──────────▶│    1. Client requests connection
  │◀──── SYN-ACK ────│    2. Server acknowledges + requests back
  │── ACK ──────────▶│    3. Client confirms
  │                   │    Connection ESTABLISHED
```

**Search:** `TCP 3-way handshake`, `TCP vs UDP`, `TCP reliable delivery`, `TCP byte stream`

### 1.4 IP Addresses & Ports

| Concept | Explanation |
|---------|-------------|
| **IP Address** | Identifies a machine on the network. `127.0.0.1` = localhost (yourself). `0.0.0.0` = all interfaces. |
| **Port** | A number (1–65535) identifying a specific service on a machine. `80` = HTTP, `443` = HTTPS, `8080` = common dev port. |
| **Socket Address** | IP + Port combined: `127.0.0.1:8080`. Uniquely identifies a service endpoint. |
| **Well-known ports** | 0–1023 (require root). **Ephemeral ports:** 49152–65535 (assigned to clients automatically). |

**Search:** `IP address and port number`, `socket address`, `well-known ports`, `ephemeral ports`

---

## 2. What is a Socket?

### 2.1 Socket Concept
A **socket** is an endpoint for communication. In Unix, it's represented as a **file descriptor** (an integer). You read/write to it just like a file.

| Analogy | Explanation |
|---------|-------------|
| Phone analogy | Socket = a phone. `bind()` = assign your phone number. `listen()` = turn it on. `accept()` = pick up a call. `send()`/`recv()` = talk. `close()` = hang up. |

### 2.2 Socket Types

| Type | Constant | Protocol | Use |
|------|----------|----------|-----|
| **Stream socket** | `SOCK_STREAM` | TCP | ✅ Webserv uses this |
| **Datagram socket** | `SOCK_DGRAM` | UDP | Not needed |
| **Raw socket** | `SOCK_RAW` | Direct IP | Not needed |

### 2.3 The Socket API (Berkeley Sockets)
The system calls in order:

```
SERVER SIDE:                          CLIENT SIDE:
socket()    → Create socket           socket()    → Create socket
bind()      → Assign address:port     connect()   → Connect to server
listen()    → Mark as passive          │
accept()    → Wait for connection  ◀───┘
recv/send() ↔ Exchange data        ↔  recv/send()
close()     → Terminate               close()
```

**Search:** `Berkeley sockets API`, `Unix socket programming`, `socket system call`, `Beej's Guide to Network Programming`

---

## 3. HTTP Protocol Fundamentals

### 3.1 What is HTTP? (HyperText Transfer Protocol)
An **application-layer**, **text-based**, **stateless**, **request-response** protocol.

| Property | Meaning |
|----------|---------|
| **Application-layer** | Runs on top of TCP |
| **Text-based** | Requests/responses are human-readable ASCII |
| **Stateless** | Each request is independent; server doesn't remember previous requests |
| **Request-response** | Client sends a request, server sends exactly one response |

**Search:** `HTTP protocol overview`, `HTTP request response model`, `HTTP stateless protocol`

### 3.2 HTTP Request Format
```
METHOD SP URI SP HTTP/VERSION CRLF     ← Request Line
Header-Name: Header-Value CRLF        ← Headers (one per line)
Header-Name: Header-Value CRLF
CRLF                                   ← Empty line (end of headers)
[optional body]                        ← Body (for POST)
```

**Example:**
```http
GET /index.html HTTP/1.1\r\n
Host: localhost:8080\r\n
Connection: keep-alive\r\n
\r\n
```

| Component | Description |
|-----------|-------------|
| **Method** | `GET`, `POST`, `DELETE` (what action to perform) |
| **URI** | `/path/to/resource` (what resource to act on) |
| **Version** | `HTTP/1.1` (protocol version) |
| **Headers** | Key-value metadata (`Host`, `Content-Length`, `Content-Type`) |
| **Body** | Data payload (present in POST, absent in GET) |

### 3.3 HTTP Response Format
```
HTTP/VERSION SP STATUS-CODE SP REASON CRLF  ← Status Line
Header-Name: Header-Value CRLF              ← Headers
CRLF                                         ← Empty line
[body]                                       ← Response body
```

**Example:**
```http
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 45\r\n
\r\n
<html><body><h1>Hello!</h1></body></html>
```

### 3.4 HTTP Methods (Verbs)

| Method | Purpose | Has Body? | Idempotent? |
|--------|---------|-----------|-------------|
| **GET** | Retrieve a resource | No | Yes |
| **POST** | Submit data / create resource | Yes | No |
| **DELETE** | Remove a resource | Optional | Yes |

**Search:** `HTTP methods GET POST DELETE`, `HTTP request format`, `HTTP response format`, `HTTP idempotent methods`

### 3.5 HTTP Status Codes

| Range | Category | Key Codes |
|-------|----------|-----------|
| **2xx** | Success | `200 OK`, `201 Created`, `204 No Content` |
| **3xx** | Redirection | `301 Moved Permanently`, `302 Found` |
| **4xx** | Client Error | `400 Bad Request`, `403 Forbidden`, `404 Not Found`, `405 Method Not Allowed`, `413 Payload Too Large` |
| **5xx** | Server Error | `500 Internal Server Error` |

**Search:** `HTTP status codes`, `HTTP status code categories`

---

## 4. Blocking vs Non-Blocking I/O

### 4.1 Blocking I/O (The Problem)
```
recv(client_fd, buffer, size, 0);
// ← Program FREEZES here until data arrives
// If client is slow, your ENTIRE server is stuck
// No other client can be served
```

A **blocking call** suspends the process until the operation completes. With one thread and blocking I/O, one slow client freezes the server for ALL clients.

### 4.2 Non-Blocking I/O (The Solution)
```c
fcntl(fd, F_SETFL, O_NONBLOCK);
// Now recv() returns IMMEDIATELY:
//   - If data is ready → returns data
//   - If no data yet   → returns -1, errno = EAGAIN
```

The call **never waits**. You check, act if ready, move on if not.

| Mode | Behavior on `recv()` with no data |
|------|-----------------------------------|
| **Blocking** | Process sleeps until data arrives |
| **Non-blocking** | Returns `-1` with `errno = EAGAIN` immediately |

**Search:** `blocking vs non-blocking I/O`, `non-blocking sockets`, `O_NONBLOCK`, `fcntl non-blocking`, `EAGAIN`

---

## 5. I/O Multiplexing

### 5.1 The Core Problem
You have 1 server process, 100 clients connected. How do you know WHICH client has data ready to read without checking each one (which would be wasteful)?

### 5.2 The Solution: I/O Multiplexing
One system call watches ALL file descriptors simultaneously and tells you which ones are ready.

| Function | OS | Scalability | Your Choice |
|----------|-----|------------|-------------|
| **`select()`** | All Unix | Poor (max 1024 FDs) | Acceptable |
| **`poll()`** | All Unix | Better (no FD limit) | ✅ Good default |
| **`epoll()`** | Linux only | Best (O(1) for ready FDs) | ✅ Best for Linux |
| **`kqueue()`** | macOS/BSD | Best (similar to epoll) | ✅ Best for macOS |

### 5.3 The Event Loop Pattern
```
while (server_running):
    ready_fds = poll(all_fds)         // "Which FDs have events?"
    for each fd in ready_fds:
        if fd == listening_socket:
            accept_new_client()       // New connection
        else if fd is readable:
            read_data(fd)             // Client sent data
        else if fd is writable:
            write_response(fd)        // Ready to send response
        else if fd has error/hangup:
            close_connection(fd)      // Client disconnected
```

**Search:** `I/O multiplexing`, `select poll epoll comparison`, `event loop pattern`, `reactor pattern`, `poll() system call`, `epoll explained`

---

## 6. CGI Concept (Common Gateway Interface)

### 6.1 What is CGI?
A standard protocol for a web server to execute an external program and return its output as an HTTP response.

```
Browser → HTTP Request → Web Server → fork() → CGI Script (Python/PHP)
                                                      │
Browser ← HTTP Response ← Web Server ← pipe() ← CGI Output (stdout)
```

### 6.2 Key CGI Concepts

| Concept | Meaning |
|---------|---------|
| **CGI Script** | An executable (Python, PHP, etc.) the server runs on behalf of the client |
| **Environment Variables** | How the server passes request info to the script (`REQUEST_METHOD`, `QUERY_STRING`, etc.) |
| **stdin** | How the server passes the request body to the script (for POST) |
| **stdout** | How the script sends its response back to the server |

### 6.3 Process Creation: fork() + execve()

| Call | What it does |
|------|-------------|
| **`fork()`** | Clones the current process. Returns 0 in child, PID in parent. |
| **`execve()`** | Replaces child process's code with the CGI script. |
| **`pipe()`** | Creates a unidirectional data channel (two FDs: read-end and write-end). |
| **`dup2()`** | Redirects stdin/stdout to pipe ends. |
| **`waitpid()`** | Parent waits for child to finish; prevents zombie processes. |

**Search:** `Common Gateway Interface`, `CGI protocol explained`, `fork execve pipe`, `Unix process creation`, `CGI environment variables RFC 3875`

---

# PART 2: PERSON A — CORE SERVER & NETWORKING (Deep Dive)

> These concepts build on the Shared Foundation. Study them in this exact order.

---

## 7. Socket Programming (Deep)

### 7.1 socket() — Creating the Endpoint

```c
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
```

| Parameter | Value | Meaning |
|-----------|-------|---------|
| **Domain** | `AF_INET` | IPv4 Internet protocol |
| **Type** | `SOCK_STREAM` | TCP (reliable, ordered byte stream) |
| **Protocol** | `0` | Let OS choose (TCP for SOCK_STREAM) |

**Returns:** A file descriptor (integer). `-1` on error.

**Search:** `socket() system call`, `AF_INET`, `SOCK_STREAM`

### 7.2 bind() — Assigning an Address

```c
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 0.0.0.0
addr.sin_port = htons(8080);
bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
```

| Concept | Meaning |
|---------|---------|
| **`sockaddr_in`** | Structure holding IPv4 address + port |
| **`INADDR_ANY`** | Bind to all available network interfaces |
| **`htons()` / `htonl()`** | Host-to-Network byte order conversion (**Endianness**) |
| **Endianness** | Network = Big-Endian. Your CPU might be Little-Endian. These functions convert. |

**Search:** `bind() system call`, `sockaddr_in structure`, `byte order endianness`, `htons htonl`, `network byte order`

### 7.3 Socket Options: setsockopt()

| Option | Purpose |
|--------|---------|
| **`SO_REUSEADDR`** | Allow reusing address immediately after server restart (avoids "Address already in use") |
| **`SO_REUSEPORT`** | Allow multiple sockets on same port (optional) |

**Search:** `setsockopt SO_REUSEADDR`, `Address already in use error`

### 7.4 listen() — Making the Socket Passive

```c
listen(sockfd, BACKLOG);
```

| Concept | Meaning |
|---------|---------|
| **Passive socket** | A socket that waits for connections (vs active socket that initiates connections) |
| **Backlog** | Max number of pending connections queued before `accept()`. Common value: `128`. |
| **SYN Queue** | OS-level queue of half-open connections (during 3-way handshake) |
| **Accept Queue** | OS-level queue of fully established connections waiting for `accept()` |

**Search:** `listen() backlog`, `TCP SYN queue accept queue`, `passive socket`

### 7.5 accept() — Accepting a Connection

```c
int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
```

| Concept | Meaning |
|---------|---------|
| **Returns a NEW FD** | The listening FD stays listening. The new FD is for THIS specific client. |
| **Client address** | `client_addr` is filled with the client's IP and port |
| **Per-connection FD** | Each client gets its own file descriptor. 100 clients = 100 FDs + 1 listening FD. |

**Search:** `accept() system call`, `listening socket vs connected socket`

### 7.6 recv() and send() — Data Transfer

| Call | Purpose | Non-blocking behavior |
|------|---------|----------------------|
| **`recv(fd, buf, len, 0)`** | Read data from socket | Returns `EAGAIN` if no data ready |
| **`send(fd, buf, len, 0)`** | Write data to socket | Returns `EAGAIN` if buffer full, or partial write |

**Critical concept — Partial Operations:**
- `recv()` may return **fewer bytes** than requested (only what's available now)
- `send()` may send **fewer bytes** than requested (kernel buffer full)
- You MUST loop and buffer until all data is read/written

**Search:** `recv() send() system call`, `partial read partial write`, `TCP send buffer`, `recv EAGAIN`

---

## 8. Non-Blocking I/O (Deep)

### 8.1 Setting Non-Blocking Mode

```c
fcntl(fd, F_SETFL, O_NONBLOCK);
```

Every socket (listening AND client) must be non-blocking. This is a **hard requirement** in the subject.

### 8.2 Non-Blocking Behavior Table

| Call | Blocking Mode | Non-Blocking Mode |
|------|--------------|-------------------|
| `accept()` | Sleeps until connection arrives | Returns `EAGAIN` if no pending connection |
| `recv()` | Sleeps until data arrives | Returns `EAGAIN` if no data |
| `send()` | Sleeps until buffer space available | Returns `EAGAIN` if buffer full |
| `connect()` | Sleeps until connected | Returns `EINPROGRESS` (not needed for server) |

### 8.3 Why Non-Blocking is Mandatory

```
BLOCKING (broken):                    NON-BLOCKING (correct):
Client A sends data slowly            Client A sends data slowly
  → Server calls recv(A)                → Server calls poll()
  → Server BLOCKS for 30 seconds        → poll says: A not ready, B ready
  → Client B, C, D all waiting           → Server reads from B immediately
  → Server is frozen                     → Checks A again next loop
                                         → All clients served fairly
```

**Search:** `fcntl O_NONBLOCK`, `non-blocking socket programming`, `blocking vs non-blocking accept`

---

## 9. I/O Multiplexing (Deep)

### 9.1 poll() — The Standard Approach

```c
struct pollfd fds[MAX_CLIENTS];
fds[0].fd = listen_fd;
fds[0].events = POLLIN;        // Watch for incoming connections

int ready = poll(fds, nfds, timeout_ms);
// ready = number of FDs with events
```

| Event | Meaning | When to use |
|-------|---------|-------------|
| **`POLLIN`** | Data available to read / new connection on listener | Always watch for this |
| **`POLLOUT`** | Ready to write without blocking | Watch when you have data to send |
| **`POLLERR`** | Error condition | Always check |
| **`POLLHUP`** | Client disconnected (hang up) | Always check |
| **`POLLNVAL`** | Invalid FD (you closed it but didn't remove from array) | Bug indicator |

**Search:** `poll() system call`, `struct pollfd`, `POLLIN POLLOUT`, `poll vs select`

### 9.2 epoll() — The Linux-Optimized Approach

```c
int epfd = epoll_create(1);                    // Create epoll instance
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);    // Register an FD
int n = epoll_wait(epfd, events, MAX, timeout); // Wait for events
```

| Function | Purpose |
|----------|---------|
| **`epoll_create()`** | Creates an epoll instance (returns an FD) |
| **`epoll_ctl()`** | Add/modify/remove FDs from the watch list |
| **`epoll_wait()`** | Block until events occur, returns ONLY ready FDs |

**Key advantage over poll():** `poll()` returns ALL FDs and you check each one. `epoll_wait()` returns ONLY the ready FDs. With 10,000 connections but only 3 active, poll checks 10,000 — epoll returns 3.

### 9.3 Edge-Triggered vs Level-Triggered

| Mode | Behavior | Trigger |
|------|----------|---------|
| **Level-triggered** (default) | `epoll_wait` keeps reporting an FD as ready as long as there's data | Safer, easier to use |
| **Edge-triggered** (`EPOLLET`) | `epoll_wait` reports an FD only ONCE when new data arrives | Faster but must read ALL data immediately |

**Search:** `epoll_create epoll_ctl epoll_wait`, `epoll edge triggered vs level triggered`, `epoll tutorial`, `C10K problem`

---

## 10. The Event Loop Architecture (Reactor Pattern)

### 10.1 The Reactor Pattern
A design pattern where a single thread waits for events on multiple handles (FDs), then dispatches to appropriate handlers. Your entire server is one big reactor.

```
┌─────────────────────────────────────────────────┐
│                 EVENT LOOP                       │
│                                                  │
│   poll()/epoll_wait()                            │
│        │                                         │
│        ├─ Listening FD ready? → accept()         │
│        │       └→ Add new client FD to poll set  │
│        │                                         │
│        ├─ Client FD readable? → recv()           │
│        │       └→ Buffer data, pass to parser    │
│        │                                         │
│        ├─ Client FD writable? → send()           │
│        │       └→ Write response from buffer     │
│        │                                         │
│        ├─ CGI Pipe FD ready? → read/write pipe   │
│        │       └→ Buffer CGI data / stream chunks│
│        │                                         │
│        ├─ Client FD error/hangup? → close()      │
│        │       └→ Remove from poll set, cleanup  │
│        │                                         │
│        └─ Timeout? → Check idle connections      │
│                └→ Drop clients past timeout       │
│                                                  │
└─────────────────────────────────────────────────┘
```

**Search:** `Reactor pattern`, `event-driven server`, `single-threaded event loop`, `event loop architecture`

### 10.2 Connection State Machine
Each client connection progresses through states:

```
   ACCEPTING
       │
       ▼
   READING_REQUEST ←──┐
       │               │ (keep-alive: read next request)
       ▼               │
   PARSING_REQUEST     │
       │               │
       ▼               │
   PROCESSING ─────────┤
       │               │
       │               ▼
       │         STATE_CGI_RUNNING (epoll watches pipes)
       │               │
       │               ▼
       │         STATE_CGI_STREAMING (streaming output)
       │               │
       ▼               │
   WRITING_RESPONSE ◀──┘
       │               │
       ├── keep-alive ─┘
       │
       ▼
   CLOSING
```

| State | What happens | Next state |
|-------|-------------|------------|
| **READING_REQUEST** | `recv()` into buffer, may need multiple reads | PARSING when `\r\n\r\n` found |
| **PARSING_REQUEST** | Parse buffer into Request object | PROCESSING |
| **PROCESSING** | Route → serve file / run CGI / handle upload | WRITING_RESPONSE |
| **WRITING_RESPONSE** | `send()` response buffer, may need multiple writes | CLOSING or READING (keep-alive) |
| **CLOSING** | `close(fd)`, remove from poll set, free resources | — |

**Search:** `connection state machine`, `HTTP connection lifecycle`, `TCP connection states`

---

## 11. File Descriptor Management

### 11.1 What is a File Descriptor?
An integer index into the kernel's per-process table of open files/sockets/pipes. Every `socket()`, `accept()`, `open()`, `pipe()` returns one. Every one MUST be `close()`-d when done.

### 11.2 FD Limits
| Limit | Typical Value | Check With |
|-------|--------------|------------|
| **Per-process soft limit** | 1024 | `ulimit -n` |
| **Per-process hard limit** | 4096–65536 | `ulimit -Hn` |
| **System-wide limit** | 100,000+ | `cat /proc/sys/fs/file-max` |

### 11.3 FD Leak
If you `accept()` a client but never `close()` the FD (even on error), you leak. After ~1000 leaks, the server can't accept new connections.

**Preventions:**
- RAII pattern in C++ (destructor calls `close()`)
- Always `close()` in error paths
- Monitor with `ls /proc/<pid>/fd | wc -l`

**Search:** `file descriptor Unix`, `file descriptor leak`, `RAII C++ resource management`, `ulimit file descriptors`

---

## 12. Signals

### 12.1 Signals Relevant to Webserv

| Signal | When | How to Handle |
|--------|------|--------------|
| **`SIGPIPE`** | You `send()` to a client that already closed its connection | **Ignore it** (`signal(SIGPIPE, SIG_IGN)`). Check `send()` return value instead. |
| **`SIGINT`** | User presses Ctrl+C | Graceful shutdown: close all FDs, free memory, exit cleanly |
| **`SIGCHLD`** | A child process (CGI) terminates | Call `waitpid()` to reap it (prevent zombies) |

**Search:** `SIGPIPE socket`, `Unix signals`, `signal handling C`, `SIGCHLD waitpid zombie process`

---

## 13. Multiple Listening Sockets

### 13.1 Why Multiple?
The subject requires: "your server must be able to listen to multiple ports." Each port = one listening socket.

```
Config says: listen 8080, listen 8081, listen 9090

→ Create socket #1, bind to 8080, listen
→ Create socket #2, bind to 8081, listen  
→ Create socket #3, bind to 9090, listen
→ Add ALL THREE to the poll set
→ Any of them can trigger accept()
```

### 13.2 Matching to Server Blocks
Multiple server blocks can share a port (distinguished by `Host` header = virtual hosting) or use different ports. Person A creates sockets based on unique `address:port` pairs from Person C's config.

**Search:** `multiple listening sockets`, `multi-port server`, `virtual hosting`

---

## 14. Timeouts & Connection Management

### 14.1 Why Timeouts?
A client can connect and then do nothing (slowloris attack, or just a broken client). Without timeouts, that FD stays open forever, wasting resources.

### 14.2 Implementation Concept
- Track `last_activity_time` per client connection
- In each event loop iteration, check all connections
- If `now - last_activity > TIMEOUT` → close the connection
- Use `poll()`'s timeout parameter to wake up periodically

**Search:** `connection timeout web server`, `slowloris attack`, `idle connection cleanup`, `poll timeout`

---

## 15. Advanced: Non-Blocking CGI & Streaming (Your Implementation)

### 15.1 Non-Blocking CGI via Epoll
Most basic servers `waitpid()` and block while a CGI runs. **Your server does not.**
When a CGI starts:
1. `fork()` and `execve()` run the script.
2. The `pipe()` FDs (stdin/stdout/stderr for the CGI) are **added to the `epoll` watch list**.
3. The server goes back to serving other clients.
4. When the CGI produces output, `epoll` triggers `_handleCgiReady()`.

### 15.2 HTTP Chunked Transfer Encoding & Streaming
If a CGI script produces 1GB of data, you cannot store it all in memory.
- **Your Solution (`STATE_CGI_STREAMING`):** As data arrives from the CGI pipe, your event loop wraps it in HTTP "Chunks" (e.g., `1A\r\n<data>\r\n`) and sends it immediately to the client. 
- You do the same for large static files (calling `response.getNextChunk()`).

**Search:** `HTTP Chunked Transfer Encoding`, `non-blocking CGI pipes`, `epoll pipe`

---

## Learning Order Summary

```
WEEK 1 (Shared):
  1. Computer Networking Fundamentals (OSI, TCP/IP, TCP)
  2. IP Addresses & Ports
  3. Socket Concept
  4. HTTP Protocol Fundamentals (Request/Response format)
  5. Blocking vs Non-Blocking I/O (concept)
  6. I/O Multiplexing (concept)
  7. CGI Concept (high-level)
  8. fork/execve/pipe (high-level)

WEEK 1-2 (Person A Deep Dive):
  7. Socket Programming Deep (socket→bind→listen→accept→recv/send)
  8. Non-Blocking I/O Deep (fcntl, EAGAIN, behavior table)
  9. I/O Multiplexing Deep (poll or epoll, events, edge vs level)
  10. Event Loop / Reactor Pattern (the main loop architecture)
  11. Connection State Machine
  12. File Descriptor Management (leaks, limits, RAII)
  13. Signals (SIGPIPE, SIGINT, SIGCHLD)
  14. Multiple Listening Sockets
  15. Timeouts & Connection Management
  16. Advanced: Non-Blocking CGI & Chunked Streaming
```
