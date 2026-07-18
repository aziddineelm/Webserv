# Socket Programming Deep Dive — Webserv Context

> Every syscall you'll use, what the OS actually does, and why you need it.

---

## The Big Picture First

```
YOUR SERVER (user space)              THE KERNEL (OS level)
┌──────────────────────┐         ┌──────────────────────────────┐
│                      │         │                              │
│  socket()  ──────────┼────────▶│  Create internal socket obj  │
│  setsockopt() ───────┼────────▶│  Modify socket settings      │
│  bind()    ──────────┼────────▶│  Reserve address:port        │
│  listen()  ──────────┼────────▶│  Create connection queues    │
│  fcntl()   ──────────┼────────▶│  Set non-blocking flag       │
│                      │         │                              │
│  poll()    ──────────┼────────▶│  "Which FDs have events?"    │
│                      │         │                              │
│  accept()  ──────────┼────────▶│  Dequeue a waiting client    │
│  recv()    ──────────┼────────▶│  Copy data from kernel buf   │
│  send()    ──────────┼────────▶│  Copy data to kernel buf     │
│  close()   ──────────┼────────▶│  Destroy socket, free port   │
│                      │         │                              │
└──────────────────────┘         └──────────────────────────────┘
```

Your code lives in **user space**. The actual networking (TCP handshakes, packet sending, buffering) happens in the **kernel**. Every syscall is you asking the kernel to do something on your behalf.

---

## 1. socket() — "Create a communication endpoint"

### Simple Terms
"Hey OS, give me a phone. I don't want to call anyone yet, I just want to own a phone."

### The Call
```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
//              │         │            └─ 0 = auto-pick protocol (TCP)
//              │         └─ SOCK_STREAM = TCP (reliable byte stream)
//              └─ AF_INET = IPv4
```

### What the OS Does
1. Allocates a **socket object** in kernel memory:
   - A **send buffer** (outgoing data waiting to be transmitted, ~128KB)
   - A **receive buffer** (incoming data waiting for your `recv()`, ~128KB)
   - A **state machine** (CLOSED → LISTEN → ESTABLISHED → etc.)
   - Protocol info (TCP, IPv4)
2. Adds an entry to your process's **file descriptor table**
3. Returns the FD number (e.g., `3`)

### Why You Need It
It's the first step. Without a socket, you have nothing — no way to listen, accept, or communicate. Every network operation starts here.

### The FD Table — What `fd = 3` Means

```
Your process's FD table:
┌────┬──────────────────┐
│ 0  │ stdin             │  ← always exists
│ 1  │ stdout            │  ← always exists
│ 2  │ stderr            │  ← always exists
│ 3  │ YOUR NEW SOCKET   │  ← socket() gave you this
└────┴──────────────────┘
```

FDs are just integers. The kernel maps each number to an internal object. `3` isn't special — it's just the next available slot.

### Return Value
- **Success:** a non-negative integer (the FD)
- **Failure:** `-1` (out of FDs, or invalid parameters)

---

## 2. setsockopt() — "Configure socket settings"

### Simple Terms
"Before I use this phone, let me change some settings — like allowing redial to the same number."

### The Call
```cpp
int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
//         │   │           │              │     └─ size of the value
//         │   │           │              └─ pointer to value (1 = enable)
//         │   │           └─ which option to set
//         │   └─ option level (SOL_SOCKET = general socket options)
//         └─ which socket
```

### What the OS Does
Flips an internal flag on the socket object:
```
Socket object in kernel:
  reuse_addr_flag: false → true
```

### Why You Need SO_REUSEADDR
Without it, this happens:
```
1. Start server on port 8080 → works
2. Stop server (Ctrl+C)
3. TCP connections enter TIME_WAIT state (kernel holds the port for ~60 seconds)
4. Restart server → bind() fails: "Address already in use"
5. Wait 60 seconds... try again... now it works
```

With `SO_REUSEADDR`:
```
1. Start server → works
2. Stop server
3. Restart immediately → works! (kernel allows binding despite TIME_WAIT)
```

### When to Call It
**After `socket()`, before `bind()`.** If you call it after `bind()`, it's too late.

---

## 3. bind() — "Assign an address to this socket"

### Simple Terms
"Give this phone a phone number — specifically, assign it to port 8080 on all my network interfaces."

### The Call
```cpp
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));          // Zero out the struct
addr.sin_family = AF_INET;               // IPv4
addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0 = all interfaces
addr.sin_port = htons(8080);             // Port 8080

bind(fd, (struct sockaddr *)&addr, sizeof(addr));
```

### What the OS Does
1. Checks if port 8080 is available (not used by another socket)
2. Creates a mapping in the kernel: **"port 8080 → this socket"**
3. When a TCP packet arrives addressed to port 8080, the kernel now knows which socket to deliver it to

```
Kernel's port table:
┌───────┬──────────────────┐
│ Port  │ Socket           │
├───────┼──────────────────┤
│ 22    │ sshd             │
│ 80    │ (unused)         │
│ 8080  │ YOUR SOCKET ←    │  bind() created this entry
│ 8081  │ (unused)         │
└───────┴──────────────────┘
```

### The sockaddr_in Struct

```
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET (2 bytes)
    in_port_t      sin_port;     // Port in NETWORK byte order (2 bytes)
    struct in_addr sin_addr;     // IP address in NETWORK byte order (4 bytes)
    char           sin_zero[8];  // Padding (unused, zero it out)
};
```

### htons() and htonl() — Byte Order

Your CPU (x86) stores `8080` as: `90 1F` (little-endian, least significant byte first)
The network expects `8080` as: `1F 90` (big-endian, most significant byte first)

| Function | Converts | Size | Use for |
|----------|----------|------|---------|
| `htons()` | Host → Network, Short | 16-bit | Ports |
| `htonl()` | Host → Network, Long | 32-bit | IP addresses |
| `ntohs()` | Network → Host, Short | 16-bit | Reading ports from packets |
| `ntohl()` | Network → Host, Long | 32-bit | Reading IPs from packets |

Forget `htons()` on the port → you bind to port `36895` instead of `8080`. Silent bug.

### INADDR_ANY (0.0.0.0)

Your machine has multiple network interfaces:
```
lo:     127.0.0.1      (localhost / loopback)
eth0:   192.168.1.100  (Ethernet / LAN)
wlan0:  192.168.1.101  (Wi-Fi)
```

- `INADDR_ANY` → accept connections on ALL of them
- `inet_addr("127.0.0.1")` → only accept connections from localhost

For Webserv, always use `INADDR_ANY` unless the config specifies an IP.

---

## 4. listen() — "Start accepting connections"

### Simple Terms
"Turn the phone on and start accepting incoming calls. Keep a queue of up to 128 waiting callers."

### The Call
```cpp
listen(fd, 128);
//     │   └─ backlog: max pending connections in the queue
//     └─ the listening socket FD
```

### What the OS Does
1. Changes the socket state: `CLOSED → LISTEN`
2. Creates **two internal queues**:

```
                         ┌─────────────────────┐
Client sends SYN ───────▶│     SYN QUEUE        │ Half-open connections
                         │  (3-way handshake    │ (SYN received, SYN-ACK sent,
                         │   in progress)       │  waiting for ACK)
                         └─────────┬────────────┘
                                   │ Handshake completes (ACK received)
                                   ▼
                         ┌─────────────────────┐
                         │    ACCEPT QUEUE      │ Fully connected clients
                         │  (max size = 128)    │ waiting for your accept()
                         └─────────┬────────────┘
                                   │ Your code calls accept()
                                   ▼
                           New client FD returned
```

### The Backlog (128)
- It's the size of the **accept queue** (fully established connections waiting for `accept()`)
- If the queue is full and a new client connects → the OS may refuse or silently drop it
- `128` is a safe default for Webserv
- Real production servers use `SOMAXCONN` (usually 128 or 4096 depending on OS)

### After listen()
The socket is now **passive**. It never sends/receives data itself. Its only purpose is to produce new connected sockets via `accept()`. It's a factory.

---

## 5. fcntl() — "Set non-blocking mode"

### Simple Terms
"Make it so this phone never puts me on hold. If nobody's calling, just tell me 'no calls right now' instantly."

### The Call
```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
//    │   │        └─ the flag to set: non-blocking mode
//    │   └─ command: "set file status flags"
//    └─ which FD
```

### What the OS Does
Flips a flag on the FD's internal entry:
```
FD 3 flags: [BLOCKING] → [NON_BLOCKING]
```

After this, EVERY I/O operation on this FD changes behavior:

| Operation | Blocking (default) | Non-blocking (O_NONBLOCK) |
|-----------|-------------------|---------------------------|
| `accept()` | Sleeps until a client connects | Returns `-1` + `EAGAIN` if no client waiting |
| `recv()` | Sleeps until data arrives | Returns `-1` + `EAGAIN` if no data ready |
| `send()` | Sleeps until buffer has space | Returns `-1` + `EAGAIN` if buffer full |

### Why EVERY Socket Must Be Non-Blocking

```
Blocking server (BROKEN):
─────────────────────────
Loop iteration 1: recv(client_5) → client_5 is slow...
                   ████████████████████████████ FROZEN for 30 seconds
                   All other 99 clients are DEAD during this time

Non-blocking server (CORRECT):
──────────────────────────────
Loop iteration 1: poll() → client_5 NOT ready, client_12 IS ready
                  recv(client_12) → got data instantly
Loop iteration 2: poll() → client_5 still not ready, client_7 IS ready
                  recv(client_7) → got data instantly
Loop iteration 3: poll() → client_5 IS ready now!
                  recv(client_5) → got data instantly
                  All clients served fairly.
```

---

## 6. poll() — "Which FDs have events?"

### Simple Terms
"I have 100 phones. Which ones are ringing right now? Don't check one by one — tell me all at once."

### The Call
```cpp
struct pollfd fds[100];
fds[0].fd = 3;          // listening socket
fds[0].events = POLLIN;  // I want to know when connections arrive
fds[1].fd = 6;           // client socket
fds[1].events = POLLIN;  // I want to know when data arrives

int ready = poll(fds, 2, 5000);  // Watch 2 FDs, timeout 5000ms
//                       └─ timeout in milliseconds (-1 = wait forever)
```

### What the OS Does
1. Your process goes to **sleep** (gives up the CPU)
2. The kernel checks all the FDs in your array
3. When ANY FD has an event (data arrived, connection ready, etc.), the kernel **wakes you up**
4. The kernel fills in the `revents` field of each `pollfd` to tell you what happened
5. Returns how many FDs have events

```
BEFORE poll():
fds[0] = {fd: 3, events: POLLIN, revents: 0}
fds[1] = {fd: 6, events: POLLIN, revents: 0}

── process sleeps ──
── client connects to port 8080 ──
── kernel wakes you up ──

AFTER poll():
fds[0] = {fd: 3, events: POLLIN, revents: POLLIN}  ← connection ready!
fds[1] = {fd: 6, events: POLLIN, revents: 0}       ← nothing happened
return value: 1 (one FD has events)
```

### The Events

| Event | Meaning | When you see it |
|-------|---------|----------------|
| `POLLIN` | Ready to read / new connection on listener | Data arrived, or a client is waiting in accept queue |
| `POLLOUT` | Ready to write without blocking | Kernel send buffer has space |
| `POLLHUP` | Client hung up (disconnected) | Client closed their end |
| `POLLERR` | Error on this FD | Something went wrong |
| `POLLNVAL` | This FD is invalid | You closed it but left it in the array (bug) |

### events vs revents

| Field | You set it | Kernel sets it |
|-------|-----------|---------------|
| `events` | ✅ "What I'm interested in" | — |
| `revents` | — | ✅ "What actually happened" |

### The Timeout

| Value | Behavior |
|-------|----------|
| `-1` | Block forever until an event occurs |
| `0` | Check and return immediately (don't wait) |
| `5000` | Wait up to 5 seconds, return early if event occurs |

For Webserv, use a small timeout (e.g., `1000` ms) so you can periodically check for idle connection timeouts.

---

## 7. accept() — "Pick up a waiting connection"

### Simple Terms
"Someone's calling on my listening phone. Give me a NEW phone connected to that caller."

### The Call
```cpp
struct sockaddr_in client_addr;
socklen_t addr_len = sizeof(client_addr);
int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
```

### What the OS Does
1. Removes one connection from the **accept queue** (created by `listen()`)
2. Creates a **brand new socket** (new FD) that's connected to that specific client
3. Returns the new FD
4. Fills `client_addr` with the client's IP and port

```
BEFORE accept():
Accept Queue: [Client_A, Client_B, Client_C]
Your FD table: {3: listener}

AFTER accept():
Accept Queue: [Client_B, Client_C]         ← Client_A removed
Your FD table: {3: listener, 6: Client_A}  ← New FD 6 created!
```

### Two Different Sockets

| | Listening socket (fd 3) | Client socket (fd 6) |
|-|------------------------|---------------------|
| **Purpose** | Factory — produces new connections | Communication — reads/writes data |
| **Created by** | `socket()` | `accept()` |
| **State** | LISTEN | ESTABLISHED |
| **You call** | `accept()` on it | `recv()`/`send()` on it |
| **Lifetime** | Entire server lifetime | One client session |

### After accept() — Don't Forget
```cpp
// The new client_fd is BLOCKING by default! Make it non-blocking:
fcntl(client_fd, F_SETFL, O_NONBLOCK);

// Add it to your poll() set:
struct pollfd pfd;
pfd.fd = client_fd;
pfd.events = POLLIN;  // Watch for incoming data
pollfds.push_back(pfd);
```

---

## 8. recv() — "Read data from a connected socket"

### Simple Terms
"The caller is talking. Read what they said into my buffer."

### The Call
```cpp
char buffer[4096];
ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);
//                                                      └─ flags (0 = normal)
```

### What the OS Does
1. Checks the socket's **kernel receive buffer** (where incoming TCP data is stored)
2. Copies data from the kernel buffer to YOUR buffer (user space)
3. Removes the copied data from the kernel buffer
4. Returns how many bytes were copied

```
Kernel receive buffer (fd 6):
[G][E][T][ ][/][\r][\n][\r][\n]  ← TCP delivered this data
 ↓ ↓ ↓ ↓ ↓  ↓   ↓  ↓   ↓
Your buffer after recv():
[G][E][T][ ][/][\r][\n][\r][\n]
bytes_read = 9
```

### Return Values — This is Critical

| Return Value | Meaning | What to Do |
|-------------|---------|-----------|
| `> 0` (e.g., 9) | Got 9 bytes of data | Append to your readBuffer, continue |
| `== 0` | **Client disconnected** gracefully | Close the FD, remove from poll, clean up |
| `== -1` + `EAGAIN` | No data available right now (non-blocking) | Normal — just wait for next poll cycle |
| `== -1` + other errno | Actual error | Close the FD, log the error |

### Partial Reads — The TCP Trap

Client sends: `"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"` (48 bytes)

But `recv()` might give you:
```
First recv():  "GET /index"         (10 bytes)  ← only part of it!
Second recv(): ".html HTTP/1.1\r\n" (17 bytes)
Third recv():  "Host: localhost\r\n" (17 bytes)
Fourth recv(): "\r\n"               (2 bytes)
```

TCP is a **byte stream** — there are no message boundaries. You might get the full request in one `recv()`, or in 10 pieces. You MUST buffer and accumulate until you have a complete HTTP request (detected by `\r\n\r\n`).

---

## 9. send() — "Write data to a connected socket"

### Simple Terms
"I want to say something to the caller. Put these bytes on the line."

### The Call
```cpp
const char *response = "HTTP/1.1 200 OK\r\n\r\nHello";
ssize_t sent = send(client_fd, response, strlen(response), 0);
```

### What the OS Does
1. Copies data from YOUR buffer into the socket's **kernel send buffer**
2. The kernel will transmit the data over TCP in the background (you don't wait for it to arrive)
3. Returns how many bytes were accepted into the kernel buffer

```
Your buffer:
[H][T][T][P][/][1][.][1][ ][2][0][0]...  (25 bytes)
                    │
                    ▼ send() copies to kernel
Kernel send buffer (fd 6):
[H][T][T][P][/][1][.][1][ ][2][0][0]...
                    │
                    ▼ Kernel transmits over TCP (asynchronously)
                  Network → Client
```

### Return Values

| Return Value | Meaning | What to Do |
|-------------|---------|-----------|
| `== 25` (all bytes) | All data accepted by kernel | Done — response sent |
| `== 10` (partial!) | Kernel buffer only had room for 10 bytes | Save the remaining 15 bytes, register `POLLOUT`, send rest later |
| `== -1` + `EAGAIN` | Kernel send buffer is completely full | Wait for `POLLOUT`, try again |
| `== -1` + `EPIPE` | Client disconnected | Close connection |

### Partial Writes — Same Problem as recv()

You want to send 50,000 bytes (a large HTML file). The kernel send buffer is ~128KB but might already be partially full.

```
send(fd, big_response, 50000, 0) → returns 32000
// Only 32000 bytes accepted! 18000 bytes still need to be sent

// You must:
// 1. Save the position: "I sent 32000, 18000 remaining"
// 2. Register POLLOUT for this client
// 3. When poll() says POLLOUT → send the remaining 18000
// 4. Maybe that also partially sends → repeat until done
```

---

## 10. close() — "Destroy a socket"

### Simple Terms
"Hang up the phone and throw it away."

### The Call
```cpp
close(fd);
```

### What the OS Does
1. Sends TCP `FIN` to the other end ("I'm done sending")
2. Frees the kernel socket object (buffers, state)
3. Removes the entry from your process's FD table
4. Releases the port (if it was a listening socket)

### Rules
- Every `socket()` needs a matching `close()`
- Every `accept()` needs a matching `close()`
- If you forget → **FD leak** → after ~1000 leaks, `socket()`/`accept()` starts failing
- Close on **every** error path, not just the success path

---

## 11. getaddrinfo() — "Resolve address for binding"

### Simple Terms
"Convert a hostname or IP string into the struct I need for `bind()`."

### The Call
```cpp
struct addrinfo hints, *res;
memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_INET;
hints.ai_socktype = SOCK_STREAM;
hints.ai_flags = AI_PASSIVE;  // For binding (server-side)

getaddrinfo(NULL, "8080", &hints, &res);
// Use res->ai_addr and res->ai_addrlen with bind()

freeaddrinfo(res);  // Don't forget to free!
```

### Why You Might Need It
It's an alternative to manually filling `sockaddr_in`. Useful if your config specifies hostnames instead of IPs. For Webserv, manually building `sockaddr_in` is simpler and sufficient.

---

## 12. signal() — "Handle OS signals"

### Simple Terms
"When someone presses Ctrl+C, don't just die — clean up first."

### The Calls You Need
```cpp
signal(SIGPIPE, SIG_IGN);     // Ignore SIGPIPE
signal(SIGINT, cleanup_handler); // Handle Ctrl+C
```

### SIGPIPE — The Silent Killer

**When:** You call `send()` on a socket, but the client already disconnected.
**Default:** Your process is **killed** instantly. No cleanup, no log, just dead.
**Fix:** `signal(SIGPIPE, SIG_IGN)` — now `send()` returns `-1` with `EPIPE` instead of killing you. You handle it gracefully.

### SIGINT — Graceful Shutdown

**When:** User presses Ctrl+C.
**Default:** Process killed immediately — FDs not closed, no cleanup.
**Fix:** Set a handler that sets a flag → your event loop checks the flag → closes all FDs → exits cleanly.

---

## The Complete Flow — All Syscalls Together

```
STARTUP:
  socket()       →  fd 3 (listener)
  setsockopt()   →  SO_REUSEADDR on fd 3
  bind()         →  fd 3 owns port 8080
  listen()       →  fd 3 accepts connections
  fcntl()        →  fd 3 is non-blocking
  signal()       →  ignore SIGPIPE

EVENT LOOP (repeats forever):
  poll(fds)      →  "fd 3 has POLLIN!" (new connection)
  accept(3)      →  fd 6 (new client)
  fcntl(6)       →  fd 6 is non-blocking
  
  poll(fds)      →  "fd 6 has POLLIN!" (client sent data)
  recv(6)        →  "GET / HTTP/1.1\r\n..."
  
  [Person B parses request, builds response]
  
  poll(fds)      →  "fd 6 has POLLOUT!" (ready to send)
  send(6)        →  "HTTP/1.1 200 OK\r\n..."
  
  poll(fds)      →  "fd 6 has POLLHUP!" (client disconnected)
  close(6)       →  cleanup

SHUTDOWN (Ctrl+C):
  close(3)       →  stop listening
  close(all)     →  close all client FDs
  exit
```
