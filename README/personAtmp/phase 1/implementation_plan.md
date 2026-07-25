# Person A — Phase 1 Implementation Plan

## Goal
By the end of Phase 1, you run `./webserv` and it:
- Listens on one or more hardcoded ports (config integration comes in Phase 3)
- Accepts telnet/curl connections and logs them
- Handles `SO_REUSEADDR`, non-blocking mode, and `SIGPIPE`
- Doesn't read/write data yet — that's Phase 2

---

## Step Breakdown

```
Step 1: Create project skeleton (files + directories)          ~15 min
Step 2: Implement Socket class                                 ~2 hours
Step 3: Implement Server class (manages multiple sockets)      ~1.5 hours
Step 4: Set up signal handling                                 ~30 min
Step 5: Wire up main.cpp                                       ~30 min
Step 6: Update Makefile                                        ~15 min
Step 7: Test with telnet                                       ~30 min
```

**Total estimated: ~5-6 hours of focused work**

---

## Step 1: Create Project Skeleton

Create empty files with the right structure:

```
srcs/server/Socket.hpp        ← Step 2
srcs/server/Socket.cpp        ← Step 2
srcs/server/Server.hpp        ← Step 3
srcs/server/Server.cpp        ← Step 3
srcs/main.cpp                 ← Step 5 (already exists, empty)
```

**Why start here:** Having the files created forces you to think about boundaries before writing code.

---

## Step 2: Implement Socket Class

### What It Is
One `Socket` object = one listening socket on one port. It wraps the lifecycle: `socket()` → `setsockopt()` → `bind()` → `listen()` → `fcntl()`.

### Interface Design

```cpp
// Socket.hpp
#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <netinet/in.h>  // sockaddr_in

class Socket {
public:
    Socket();                              // Default constructor
    Socket(const Socket &other);           // Copy constructor
    Socket &operator=(const Socket &other); // Assignment operator
    ~Socket();                             // Destructor — closes FD

    // Setup: creates, binds, listens on the given port
    // Returns true on success, false on failure
    bool    setup(int port);

    // Getters
    int     getFd() const;
    int     getPort() const;

private:
    int                 _fd;        // The listening socket FD
    int                 _port;      // The port number
    struct sockaddr_in  _addr;      // The bound address

    // Internal helpers
    bool    _createSocket();         // socket()
    bool    _setOptions();           // setsockopt(SO_REUSEADDR)
    bool    _bindSocket();           // bind()
    bool    _startListening();       // listen()
    bool    _setNonBlocking();       // fcntl(O_NONBLOCK)
};

#endif
```

### Implementation Order (inside Socket.cpp)

Implement each private helper **one at a time**, test mentally, then move to the next:

**2a. Constructor / Destructor**
```
- Constructor: set _fd = -1, _port = 0
- Destructor: if (_fd != -1) close(_fd)
- This is RAII — the socket cleans itself up
```

**2b. `_createSocket()`**
```
- Call socket(AF_INET, SOCK_STREAM, 0)
- Store result in _fd
- If -1 → log error with strerror(errno), return false
- Return true
```

**2c. `_setOptions()`**
```
- int opt = 1
- Call setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
- If -1 → log error, close(_fd), return false
- Return true
```

**2d. `_bindSocket()`**
```
- Fill _addr: sin_family = AF_INET, sin_addr = htonl(INADDR_ANY), sin_port = htons(_port)
- Call bind(_fd, (sockaddr*)&_addr, sizeof(_addr))
- If -1 → log error ("Address already in use" is common), close(_fd), return false
- Return true
```

**2e. `_startListening()`**
```
- Call listen(_fd, 128)
- If -1 → log error, close(_fd), return false
- Return true
```

**2f. `_setNonBlocking()`**
```
- Call fcntl(_fd, F_SETFL, O_NONBLOCK)
- If -1 → log error, close(_fd), return false
- Return true
```

**2g. `setup(int port)` — ties them all together**
```
- _port = port
- Call each helper in order: _createSocket → _setOptions → _bindSocket → _startListening → _setNonBlocking
- If any returns false → return false (helper already closed _fd)
- Log: "Listening on port [port] (fd [_fd])"
- Return true
```

### Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| RAII (close in destructor) | ✅ Yes | Prevents FD leaks — if a Socket goes out of scope, FD is auto-closed |
| Separate private helpers | ✅ Yes | Each syscall isolated = easy to debug which step failed |
| `setup()` returns bool | ✅ Yes | C++98 has limited exceptions. Bool is simple and clear |
| Orthodox Canonical Form | ✅ Yes | 42 requirement. Copy/assign must handle FD ownership carefully |

### Copy/Assignment Warning
Socket owns an FD. If you copy a Socket, you now have **two objects pointing to the same FD**. When one destructs, it closes the FD — the other now has a **dangling FD**. Options:
- Make Socket **non-copyable** (private copy ctor + assignment, don't implement them)
- Or use `dup()` in the copy constructor to duplicate the FD

**Recommendation:** Make it non-copyable for now. You won't need to copy sockets.

---

## Step 3: Implement Server Class

### What It Is
The `Server` class owns all `Socket` objects and will later own the event loop (Phase 2). For Phase 1, it just creates sockets and accepts connections in a simple loop.

### Interface Design

```cpp
// Server.hpp
#ifndef SERVER_HPP
#define SERVER_HPP

#include "Socket.hpp"
#include <vector>

class Server {
public:
    Server();
    ~Server();

    // Initialize: create listening sockets for each port
    bool    init(const std::vector<int> &ports);

    // Run: simple accept loop (Phase 1 version — replaced by event loop in Phase 2)
    void    run();

    // Graceful shutdown
    void    stop();

private:
    std::vector<Socket *>   _sockets;     // One Socket per port
    bool                    _running;      // Flag for the main loop

    // Disable copy (Server should never be copied)
    Server(const Server &);
    Server &operator=(const Server &);
};

#endif
```

### Implementation Order

**3a. `init(ports)`**
```
- For each port in the vector:
    - Create a new Socket (new Socket())
    - Call socket->setup(port)
    - If fails → log error, clean up all already-created sockets, return false
    - Push to _sockets vector
- Log: "Server initialized with [N] listening sockets"
- Return true
```

**3b. `run()` — Phase 1 Simple Version**
```
- Set _running = true
- While _running:
    - For each Socket in _sockets:
        - Try accept(socket->getFd(), ...) 
        - If returns a valid FD → log "New client on port X (fd Y)"
        - Then immediately close the client FD (we can't do anything with it yet)
    - usleep(100000) to avoid busy-spinning (100ms pause)
        ↑ This is a TEMPORARY hack. Phase 2 replaces this with poll()
```

**3c. `stop()`**
```
- Set _running = false
```

**3d. Destructor**
```
- For each Socket pointer in _sockets:
    - delete it (which triggers Socket destructor → close(fd))
- Clear the vector
```

### Why `Socket *` (pointers) in the vector?
In C++98, `std::vector` copies elements when resizing. If Socket held an FD and got copied, the old copy's destructor would close the FD — destroying the one in the vector. Using pointers avoids this. The Server owns the pointers and deletes them in its destructor.

---

## Step 4: Signal Handling

### What to Do
Create a small setup in `main.cpp` or a utility:

```cpp
#include <csignal>

// Global flag for graceful shutdown
volatile sig_atomic_t g_running = 1;

void signalHandler(int signum) {
    (void)signum;
    g_running = 0;  // Server loop will check this and exit
}

// Call this before server.run():
void setupSignals() {
    signal(SIGPIPE, SIG_IGN);       // Don't crash on broken pipes
    signal(SIGINT, signalHandler);   // Ctrl+C → graceful shutdown
    signal(SIGQUIT, signalHandler);  // Ctrl+\ → graceful shutdown
}
```

### Why Each Signal

| Signal | Trigger | Without handling | With handling |
|--------|---------|-----------------|---------------|
| `SIGPIPE` | `send()` to disconnected client | **Server dies silently** | `send()` returns -1 instead |
| `SIGINT` | Ctrl+C | Server killed, FDs leaked | Set flag, clean shutdown |
| `SIGQUIT` | Ctrl+\\ | Core dump | Clean shutdown |

---

## Step 5: Wire Up main.cpp

```cpp
// main.cpp
#include "server/Server.hpp"
#include <csignal>
#include <iostream>
#include <vector>

volatile sig_atomic_t g_running = 1;

void signalHandler(int signum) {
    (void)signum;
    g_running = 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // Config integration comes later (Phase 3, from Person C)
    // For now, hardcode ports
    std::vector<int> ports;
    ports.push_back(8080);
    ports.push_back(8081);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signalHandler);

    Server server;
    if (!server.init(ports)) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }

    server.run();  // Blocks until Ctrl+C sets g_running = 0

    return 0;
}
```

### Note on `g_running`
The `Server::run()` loop should check `g_running` (the global flag) to know when to exit. This means `Server::run()` needs access to this flag — either pass it, or check it directly as an `extern` global.

---

## Step 6: Update Makefile

```makefile
SRCS = srcs/main.cpp \
       srcs/server/Socket.cpp \
       srcs/server/Server.cpp
```

Just update the `SRCS` variable. Your existing Makefile rules already handle the rest.

---

## Step 7: Test

### Build and Run
```bash
make re
./webserv
```

Expected output:
```
Listening on port 8080 (fd 3)
Listening on port 8081 (fd 4)
Server initialized with 2 listening sockets
```

### Test 1: Single telnet connection
```bash
# In another terminal:
telnet localhost 8080
```
Server should print: `New client connected on port 8080 (fd 5)`

### Test 2: Multiple ports
```bash
telnet localhost 8081
```
Should also work.

### Test 3: Restart resilience
```bash
# Ctrl+C to stop server
# Immediately restart:
./webserv
# Should NOT get "Address already in use" (SO_REUSEADDR works)
```

### Test 4: Multiple simultaneous clients
```bash
# Open 3 terminals, all connect:
telnet localhost 8080
telnet localhost 8080
telnet localhost 8080
# All 3 should connect
```

---

## Summary: The 7 Steps in Order

| Step | What You Build | What You Can Test After |
|------|---------------|----------------------|
| 1 | File skeleton | Files exist, project compiles (empty) |
| 2 | Socket class | Nothing yet (no main) |
| 3 | Server class | Nothing yet (no main) |
| 4 | Signal handling | Nothing yet |
| 5 | main.cpp | `./webserv` starts, listens, accepts telnet |
| 6 | Makefile | `make` compiles everything |
| 7 | Testing | Verify all 4 test scenarios pass |

> [!TIP]
> **Practical order:** Do steps 1 + 6 first (skeleton + Makefile) so you can compile after every change. Then 2 → 3 → 4 → 5 → 7, compiling and testing incrementally.

---

## What's Next (Phase 2 Preview)

Phase 1 ends with a server that accepts connections but does nothing with them. Phase 2 replaces the simple `accept()` loop with the **poll() event loop**:

```
Phase 1 loop (temporary):        Phase 2 loop (real):
while (running):                  while (running):
    for each listener:                poll(all_fds)
        try accept()                  for each ready FD:
    usleep(100ms)                         if listener → accept
                                          if client POLLIN → recv
                                          if client POLLOUT → send
                                          if POLLHUP → close
```

Your Phase 1 code (Socket class, Server class, signal handling) carries forward unchanged. Only `Server::run()` gets rewritten.
