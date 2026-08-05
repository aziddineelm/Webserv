# EventLoop & Core Server — Architecture

> Technical documentation for the non-blocking, event-driven core of Webserv.  
> This module handles all network I/O, connection lifecycle management, and CGI process integration using the Linux `epoll` API.

---

## Table of Contents

1. [Class Map](#1-class-map)
2. [Startup Sequence](#2-startup-sequence)
3. [I/O Multiplexing Engine](#3-io-multiplexing-engine)
4. [Socket Initialization](#4-socket-initialization)
5. [Event Loop Lifecycle](#5-event-loop-lifecycle)
6. [Client State Machine](#6-client-state-machine)
7. [Non-Blocking CGI Pipeline](#7-non-blocking-cgi-pipeline)
8. [Module Integration](#8-module-integration)
9. [File Descriptor Ownership & Leak Prevention](#9-file-descriptor-ownership--leak-prevention)
10. [Signal Handling & Graceful Shutdown](#10-signal-handling--graceful-shutdown)
11. [Reference Tables](#11-reference-tables)

---

## 1. Class Map

Complete class hierarchy showing how all modules connect. The `Client` struct is the central integration point — it holds one instance of each module's core object.

```mermaid
classDiagram
    direction TB

    class MainCpp {
        +g_running : sig_atomic_t
        +signalHandler(int)
        +setupSignals()
        +main(argc, argv)
    }

    class ConfigParser {
        +parse()
        +validate()
        +getPorts() vector~int~
        +getServers() vector~ServerConfig~
    }

    class ServerConfig {
        +listen_ports : vector~uint16_t~
        +host : string
        +server_names : vector~string~
        +root : string
        +index : string
        +client_max_body_size : size_t
        +error_pages : map~int string~
        +locations : map~string LocationConfig~
        +matchLocation(uri) LocationConfig
        +getLocationList() vector~LocationConfig~
    }

    class LocationConfig {
        +path : string
        +allowed_methods : vector~string~
        +root : string
        +autoindex : bool
        +index : string
        +redirect_url : string
        +redirect_code : int
        +upload_store : string
        +cgi_extensions : vector~string~
        +cgi_path : string
        +cgi_map : map~string string~
        +client_max_body_size : size_t
    }

    class Server {
        -_sockets : vector~Socket~
        -_eventLoop : EventLoop
        +init(ports, configs) bool
        +run()
        +stop()
    }

    class Socket {
        -_fd : int
        -_port : int
        -_addr : sockaddr_in
        +setup(port) bool
        -_createSocket() bool
        -_setOptions() bool
        -_bindSocket() bool
        -_startListening() bool
        -_setNonBlocking() bool
    }

    class EventLoop {
        -_epollFd : int
        -_clients : map~int Client~
        -_listenPorts : map~int int~
        -_cgiToClient : map~int int~
        -_configs : vector~ServerConfig~
        -_running : bool
        +setConfigs(configs)
        +addListenFd(fd, port)
        +run()
        +stop()
        -_handleAccept(listenFd)
        -_handleRead(clientFd)
        -_handleWrite(clientFd)
        -_handleCgiReady(pipeFd, events)
        -_handleDisconnect(clientFd)
        -_reloadWriteBuffer(clientFd, client) bool
        -_handleKeepAlive(clientFd, client)
        -_dispatchRequest(clientFd, client)
        -_spawnCgi(clientFd, client, config)
        -_startWriting(client)
        -_checkTimeouts()
    }

    class Client {
        +fd : int
        +state : ClientState
        +request : Request
        +response : Response
        +cgi : CGIHandler
        +writeBuffer : string
        +writeOffset : size_t
        +lastActivity : time_t
        +listenPort : int
    }

    class Request {
        +feed(data)
        +isComplete() bool
        +hasError() bool
        +reset()
        +getMethod() string
        +getUri() string
        +getHeader(key) string
        +getBodyFilePath() string
        +isKeepAlive() bool
    }

    class Router {
        +handleRequest(req, res, locations)
        +resolveVirtualHost(req, port, configs) ServerConfig
        +matchLocation(uri, locations) LocationConfig
    }

    class Response {
        +isCgi() bool
        +getCgiScript() string
        +getCgiInterpreter() string
        +buildErrorPage(code)
        +buildFromCgiHeaders(headers)
        +getNextChunk() string
        +isDone() bool
        +setHeader(key, value)
        +formatChunk(data) string
    }

    class CGIHandler {
        -_state : CgiState
        -_pid : int
        -_stdinFd : int
        -_stdoutFd : int
        -_stderrFd : int
        +startFromRequest(req, config, script, interp) bool
        +onStdinReady()
        +onStdoutReady()
        +onStderrReady()
        +headersReady() bool
        +hasPendingOutput() bool
        +popOutput() string
        +outputFullyConsumed() bool
        +checkTimeout() bool
        +getState() CgiState
    }

    MainCpp ..> ConfigParser : parses config file
    MainCpp ..> Server : creates and runs
    ConfigParser --> ServerConfig : produces
    ServerConfig *-- LocationConfig : contains
    Server *-- Socket : owns via heap
    Server *-- EventLoop : owns via stack
    EventLoop *-- Client : tracks in _clients map
    EventLoop ..> Router : calls for dispatch
    EventLoop ..> ServerConfig : reads from _configs
    Client *-- Request : owns (HTTP module)
    Client *-- Response : owns (HTTP module)
    Client *-- CGIHandler : owns (CGI module)
    Router ..> Request : reads
    Router ..> Response : writes
    Router ..> LocationConfig : matches against
```

> [!NOTE]
> **Module boundaries:** `Server`, `Socket`, `EventLoop`, and `Client` belong to the core server module (`srcs/server/`). `Request`, `Response`, and `Router` belong to the HTTP module (`srcs/http/`). `ConfigParser`, `ServerConfig`, and `CGIHandler` belong to the configuration and CGI module (`srcs/config/` + `srcs/cgi/`).

---

## 2. Startup Sequence

Traces the exact order of operations from `./webserv config/default.conf` to the first `epoll_wait()` call.

```mermaid
sequenceDiagram
    participant U as User Shell
    participant M as main.cpp
    participant CP as ConfigParser
    participant S as Server
    participant Sk as Socket
    participant EL as EventLoop
    participant K as Kernel

    U->>M: ./webserv config/default.conf
    
    Note over M: 1. setupSignals() sets<br/>SIGPIPE to SIG_IGN and<br/>SIGINT/SIGQUIT to signalHandler

    M->>CP: ConfigParser configPath then parse and validate
    CP-->>M: Returns parsed ServerConfigs and port list
    
    Note over M: 2. Create Server object<br/>server.init ports, configs

    M->>S: server.init(ports, configs)
    S->>EL: _eventLoop.setConfigs(configs)
    
    Note over S: 3. Loop over each unique port

    loop For each port in ports
        S->>Sk: new Socket
        S->>Sk: sock.setup(port)
        Sk->>K: socket(AF_INET, SOCK_STREAM, 0)
        Sk->>K: setsockopt(SO_REUSEADDR)
        Sk->>K: bind(fd, addr, sizeof)
        Sk->>K: listen(fd, 128)
        Sk->>K: fcntl(fd, F_SETFL, O_NONBLOCK)
        Sk-->>S: returns true - fd is ready
        S->>EL: _eventLoop.addListenFd(fd, port)
        EL->>K: epoll_ctl(EPOLL_CTL_ADD, listenFd, EPOLLIN)
    end

    M->>S: server.run()
    S->>EL: _eventLoop.run()
    
    Note over EL: 4. Enter main loop<br/>while (_running && g_running)

    EL->>K: epoll_wait — Server is now listening
```

**Key source files:** [main.cpp](srcs/main.cpp) → [Server.cpp](srcs/server/Server.cpp) → [Socket.cpp](srcs/server/Socket.cpp) → [EventLoop.cpp](srcs/server/EventLoop.cpp)

---

## 3. I/O Multiplexing Engine

### Why `epoll` Over `poll` / `select`

| Feature | `select()` | `poll()` | `epoll()` |
|---------|-----------|---------|----------|
| **Complexity per call** | O(N) — scans all FDs | O(N) — scans all FDs | O(1) — returns only ready FDs |
| **FD limit** | 1024 (FD_SETSIZE) | No hard limit | No hard limit |
| **FD registration** | Rebuild `fd_set` every loop | Rebuild `pollfd[]` every loop | Register once with `epoll_ctl` |
| **Kernel mechanism** | Linear scan | Linear scan | Red-Black Tree + Ready List |
| **Scales to** | ~hundreds | ~thousands | ~millions |

**Why this matters for Webserv:** Under `siege -c 250` stress testing, `poll()` would scan 250+ FDs every single loop iteration. `epoll` only wakes up and returns the exact FDs that actually have data ready.

### How `epoll` Works Inside the Kernel

```mermaid
flowchart TB
    subgraph User ["User Space (Server Code)"]
        Create["epoll_create(1024)\n→ Returns _epollFd"]
        Ctl["epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev)\n→ Registers FD into kernel tree"]
        Wait["epoll_wait(_epollFd, events[], 1024, 1000ms)\n→ Blocks until events are ready"]
    end

    subgraph Kernel ["Kernel Space"]
        RBT["🌳 Red-Black Tree\n(Interest List)\nStores ALL registered FDs\nInsertion: O(log N)"]
        RL["📋 Ready List\n(Doubly-Linked List)\nOnly FDs with pending events\nAccess: O(1)"]
        NIC["⚡ Hardware NIC Interrupt\nNetwork card receives TCP packet"]
        CB["Callback Function\nep_poll_callback()"]
    end

    Create -->|"Allocates epoll instance"| RBT
    Ctl -->|"Insert/modify FD"| RBT
    NIC -->|"Triggers interrupt handler"| CB
    CB -->|"Moves FD from tree to ready list"| RL
    Wait <-.->|"Sleeps until RL non-empty\nReturns only ready events"| RL
```

**The key insight:** When data arrives on a socket, the kernel's network card triggers a hardware interrupt. The interrupt handler calls `ep_poll_callback()` which moves that specific FD from the Red-Black Tree to the Ready List. When `epoll_wait()` returns, it copies only the Ready List entries to the `events[]` array — zero scanning.

### The Three `epoll` Syscalls

| Syscall | What It Does | Usage |
|---------|-------------|-------|
| `epoll_create(1024)` | Creates the epoll instance (size hint is ignored in modern kernels but required by the API) | Constructor |
| `epoll_ctl(EPOLL_CTL_ADD)` | Register a new FD to monitor | `_addEpollFd()` |
| `epoll_ctl(EPOLL_CTL_MOD)` | Switch between `EPOLLIN` ↔ `EPOLLOUT` | `_setEpollEvents()` |
| `epoll_ctl(EPOLL_CTL_DEL)` | Remove FD from monitoring | `_removeEpollFd()` |
| `epoll_wait(...)` | Block until events are ready (or timeout) | Main loop |

> [!NOTE]
> **Level-Triggered mode (default)** is used instead of Edge-Triggered. This means `epoll_wait()` keeps reporting a FD as ready as long as there is data in the buffer. This is safer and simpler — there is no need to drain every byte in a single call.

---

## 4. Socket Initialization

Every listening socket goes through exactly **5 sequential syscalls**. If any fails, the chain breaks and the FD is cleaned up via RAII:

```mermaid
flowchart LR
    S1["socket(AF_INET,\nSOCK_STREAM, 0)"] --> S2["setsockopt(\nSOL_SOCKET,\nSO_REUSEADDR)"]
    S2 --> S3["bind(fd,\nsockaddr_in,\nsizeof)"]
    S3 --> S4["listen(fd, 128)"]
    S4 --> S5["fcntl(fd,\nF_SETFL,\nO_NONBLOCK)"]
```

| Syscall | Purpose | What Happens If Missing |
|---------|---------|----------------------|
| `socket()` | Creates a TCP endpoint (IPv4, stream-based) | No network communication possible |
| `setsockopt(SO_REUSEADDR)` | Allow immediate rebind after restart (skip `TIME_WAIT`) | *"Address already in use"* error when restarting server |
| `bind()` | Associates socket with `0.0.0.0:port` — `htonl(INADDR_ANY)` binds to all interfaces | Socket exists but isn't attached to any network address |
| `listen(fd, 128)` | Marks socket as passive (accepting connections). Backlog=128 means kernel queues up to 128 pending `SYN` handshakes | `accept()` would fail — socket isn't in listening state |
| `fcntl(O_NONBLOCK)` | Makes `accept()` return `EAGAIN` instead of blocking when no connections pending | `accept()` on listener blocks entire server |

---

## 5. Event Loop Lifecycle

Complete flowchart of `EventLoop::run()` — the heart of the server:

```mermaid
flowchart TD
    Start(["EventLoop::run()"]) --> Loop{"while\n(_running && g_running)"}
    Loop -->|"true"| Wait["numEvents = epoll_wait(\n_epollFd, events, 1024, 1000ms)"]
    
    Wait --> CheckErr{"numEvents < 0?"}
    CheckErr -->|"errno == EINTR\n(signal interrupted)"| Loop
    CheckErr -->|"Other error"| Break["break → shutdown"]
    
    CheckErr -->|"numEvents >= 0"| ForLoop["for i = 0..numEvents"]
    
    ForLoop --> GetFD["fd = events[i].data.fd\nrevents = events[i].events"]
    
    GetFD --> IsListener{"fd in\n_listenPorts?"}
    IsListener -->|"Yes"| Accept["_handleAccept(fd)"]
    
    Accept --> AcceptWhile["while(true):\nclientFd = accept(listenFd, ...)"]
    AcceptWhile --> AcceptCheck{"clientFd == -1?"}
    AcceptCheck -->|"errno == EAGAIN\n(queue drained)"| NextEvent["continue to next event"]
    AcceptCheck -->|"Valid FD"| SetNB["fcntl(clientFd, F_SETFL, O_NONBLOCK)"]
    SetNB --> AddClient["_addEpollFd(clientFd, EPOLLIN)\n_clients[clientFd] = Client(clientFd, port)"]
    AddClient --> AcceptWhile
    
    IsListener -->|"No"| IsCgi{"fd in\n_cgiToClient?"}
    IsCgi -->|"Yes"| CgiReady["_handleCgiReady(fd, revents)"]
    
    IsCgi -->|"No"| IsErr{"revents &\nEPOLLERR|EPOLLHUP?"}
    IsErr -->|"Yes"| Disconnect["_handleDisconnect(fd)"]
    
    IsErr -->|"No"| IsRead{"revents &\nEPOLLIN?"}
    IsRead -->|"Yes"| Read["_handleRead(fd)"]
    
    Read --> Recv["bytesRead = recv(fd, buf, 8192, 0)"]
    Recv --> RecvCheck{"bytesRead?"}
    RecvCheck -->|"== 0 (FIN)"| Disconnect
    RecvCheck -->|"< 0 (error)"| Disconnect
    RecvCheck -->|"> 0"| Feed["client.request.feed(buf, bytesRead)"]
    Feed --> Complete{"request.isComplete()\n|| hasError()?"}
    Complete -->|"Yes"| Dispatch["_dispatchRequest(clientFd, client)"]
    Complete -->|"No (partial)"| NextEvent

    Dispatch --> VHost["Router::resolveVirtualHost(\nreq, listenPort, _configs)"]
    VHost --> Route["Router::handleRequest(\nreq, res, locations)"]
    Route --> IsCgiRoute{"response.isCgi()?"}
    IsCgiRoute -->|"Yes"| SpawnCgi["_spawnCgi(clientFd, client, config)"]
    IsCgiRoute -->|"No"| StartWrite["_startWriting(client)\n_setEpollEvents(fd, EPOLLOUT)"]
    
    IsRead -->|"client still exists"| IsWrite{"revents &\nEPOLLOUT?"}
    IsWrite -->|"Yes"| Write["_handleWrite(fd)"]
    
    Write --> Send["bytesSent = send(fd,\nbuf + offset, remaining, 0)"]
    Send --> SendCheck{"writeOffset >=\nwriteBuffer.size()?"}
    SendCheck -->|"No (partial)"| AdvOffset["writeOffset += bytesSent"]
    SendCheck -->|"Yes (buffer empty)"| Reload["_reloadWriteBuffer(clientFd, client)"]
    Reload -->|"true: more data loaded"| NextEvent
    Reload -->|"false: response done"| KeepAlive["_handleKeepAlive(clientFd, client)"]
    KeepAlive -->|"keep-alive"| Reset["Reset client → STATE_READING\n_setEpollEvents(fd, EPOLLIN)"]
    KeepAlive -->|"close"| Disconnect
    
    NextEvent --> ForLoop
    ForLoop -->|"All events processed"| Timeouts["_checkTimeouts()"]
    Timeouts --> Loop
    
    Loop -->|"false"| Shutdown(["Shutdown"])
```

---

## 6. Client State Machine

Each client connection is tracked by a `Client` struct with 4 active states defined in the `ClientState` enum:

```mermaid
stateDiagram-v2
    [*] --> STATE_READING : accept() new client, set EPOLLIN

    STATE_READING --> STATE_READING : partial recv(), feed to parser
    STATE_READING --> STATE_WRITING : request complete, static route
    STATE_READING --> STATE_CGI_RUNNING : request complete, CGI route
    STATE_READING --> [*] : recv() returns 0 or error, or timeout

    STATE_CGI_RUNNING --> STATE_CGI_RUNNING : absorbing CGI headers from stdout
    STATE_CGI_RUNNING --> STATE_CGI_STREAMING : headers parsed, start chunked stream
    STATE_CGI_RUNNING --> STATE_WRITING : CGI done (small output, no streaming)
    STATE_CGI_RUNNING --> [*] : CGI timeout → send 504

    STATE_CGI_STREAMING --> STATE_CGI_STREAMING : streaming HTML chunks to browser
    STATE_CGI_STREAMING --> STATE_WRITING : CGI finished + all output consumed
    STATE_CGI_STREAMING --> [*] : CGI timeout → terminate stream

    STATE_WRITING --> STATE_WRITING : partial send(), advance writeOffset
    STATE_WRITING --> STATE_READING : all sent + keep-alive → reset client
    STATE_WRITING --> [*] : all sent + connection close
```

### Buffer Management: How Partial I/O Works

```
 Client struct (per connection)
 ┌─────────────────────────────────────────────────────────────┐
 │ fd = 7                                                      │
 │ listenPort = 8080                                           │
 │ state = STATE_WRITING                                       │
 │ lastActivity = 1752659145                                   │
 │                                                             │
 │ request (HTTP module):                                      │
 │   ┌─ _buffer: accumulated raw bytes from recv() calls       │
 │   ├─ _state: REQUEST_LINE → HEADERS → BODY → COMPLETE      │
 │   └─ feed(): appends new data, advances parser state        │
 │                                                             │
 │ response (HTTP module):                                     │
 │   ┌─ getNextChunk(): returns next piece of response         │
 │   └─ isDone(): true when all chunks sent                    │
 │                                                             │
 │ writeBuffer = "HTTP/1.1 200 OK\r\n..."  ← current chunk    │
 │ writeOffset = 4096  ← bytes already sent from this chunk    │
 │              ▲                                              │
 │              │ send() returned 4096 on the last EPOLLOUT    │
 │              │ Next send() starts from writeBuffer + 4096   │
 │                                                             │
 │ cgi (CGI module):                                           │
 │   ┌─ _stdinFd / _stdoutFd / _stderrFd: pipe endpoints      │
 │   ├─ _pid: child process PID                                │
 │   └─ _state: CGI_IDLE → CGI_RUNNING → CGI_DONE             │
 └─────────────────────────────────────────────────────────────┘
```

---

## 7. Non-Blocking CGI Pipeline

The most complex subsystem in the core server. The key design decision: **CGI pipe FDs are registered into the same `epoll` instance as client sockets**, so the event loop never blocks waiting for a CGI script.

### Step 1: Spawning the CGI Process

```mermaid
flowchart TD
    D["_dispatchRequest()\nresponse.isCgi() == true"] --> Spawn["_spawnCgi(clientFd, client, config)"]
    
    Spawn --> Start["client.cgi.startFromRequest(\nreq, config, scriptPath, interpreter)"]
    
    Start --> Fork["CGIHandler internally:\npipe(cgiIn) + pipe(cgiOut) + pipe(cgiErr)\nfork() → child: dup2 + execve()\nparent: close child-side FDs"]
    
    Fork --> Pause["_setEpollEvents(clientFd, 0)\nSuspend client socket (sleep mode)"]
    
    Pause --> RegStdout["_addEpollFd(stdoutFd, EPOLLIN)\n_cgiToClient[stdoutFd] = clientFd"]
    Pause --> RegStderr["_addEpollFd(stderrFd, EPOLLIN)\n_cgiToClient[stderrFd] = clientFd"]
    Pause --> RegStdin["_addEpollFd(stdinFd, EPOLLOUT)\n_cgiToClient[stdinFd] = clientFd"]
    
    RegStdout --> Back["Return to epoll_wait()\nServer continues serving OTHER clients!"]
    RegStderr --> Back
    RegStdin --> Back
```

> [!NOTE]
> **Why `_setEpollEvents(clientFd, 0)` instead of removing from epoll?**  
> Setting events to `0` keeps the FD in epoll but disables `EPOLLIN` and `EPOLLOUT`. The kernel **always** forces `EPOLLERR` and `EPOLLHUP` — so if the client closes their browser during CGI execution, the server still detects it and cleans up.

### Step 2: The CGI State Machine (Headers vs Body)

```mermaid
sequenceDiagram
    participant Browser
    participant EventLoop
    participant CGIHandler
    participant Python as CGI Script

    Note over EventLoop: STATE_CGI_RUNNING
    
    Python->>CGIHandler: Prints "Content-Type: text/html\r\n\r\n"
    CGIHandler->>EventLoop: _handleCgiReady() → headersReady() == true
    EventLoop->>EventLoop: Parse CGI headers, build HTTP response headers
    EventLoop->>Browser: Send "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
    
    Note over EventLoop: STATE_CGI_STREAMING
    
    Python->>CGIHandler: Prints "<h1>Hello</h1>"
    CGIHandler->>EventLoop: _handleCgiReady() → hasPendingOutput() == true
    EventLoop->>Browser: Send "f\r\n<h1>Hello</h1>\r\n" (chunked encoding)
    
    Python->>CGIHandler: Prints "<p>World</p>"
    CGIHandler->>EventLoop: _handleCgiReady() → hasPendingOutput() == true
    EventLoop->>Browser: Send "c\r\n<p>World</p>\r\n" (chunked encoding)
    
    Python->>CGIHandler: Process exits (EOF on stdout)
    CGIHandler->>EventLoop: getState() == CGI_DONE && outputFullyConsumed()
    
    Note over EventLoop: STATE_WRITING
    EventLoop->>Browser: Send "0\r\n\r\n" (terminal chunk)
    Note over EventLoop: Response complete!
```

**The two CGI states explained:**
- **`STATE_CGI_RUNNING`:** The server is absorbing data from the CGI script but NOT sending anything to the browser. It is searching for the `\r\n\r\n` that separates CGI headers from the body.
- **`STATE_CGI_STREAMING`:** The headers are parsed. Everything the script prints from now on is content. The server wraps it in Chunked Transfer Encoding and streams it directly to the browser.

### Step 3: Producer-Consumer Data Flow

The CGI data flow follows a **Producer-Consumer** pattern between two different epoll events:

| Role | Function | Triggered By | What It Does |
|------|----------|-------------|-------------|
| **Producer** | `_handleCgiReady()` | `EPOLLIN` on CGI pipe | Reads output from CGI script, stores in buffer, wakes up network socket |
| **Consumer** | `_reloadWriteBuffer()` | `EPOLLOUT` on client socket | Pulls data from buffer, formats as HTTP chunk, sends to browser |

**Scenario A — Network faster than CGI script:**
The script is slow. The Consumer sends data faster than the Producer generates it. When the buffer empties, `_reloadWriteBuffer()` puts the client socket to sleep (`_setEpollEvents(clientFd, 0)`). When the script finally prints more, `_handleCgiReady()` wakes it back up.

**Scenario B — CGI script faster than network:**
The script floods 100 chunks instantly. The Producer stores them all in memory. The Consumer slowly drains them one by one as the network allows, without needing the Producer to wake it up.

### Step 4: CGI Timeout Handling

If a CGI script hangs (infinite loop, deadlock), `_checkTimeouts()` detects and terminates it:

```mermaid
flowchart TD
    CT["_checkTimeouts()"] --> Check{"client.state ==\nSTATE_CGI_RUNNING or STREAMING\n&& cgi.checkTimeout()?"}
    
    Check -->|"Yes (timeout!)"| CleanPipes["Remove ALL CGI pipe FDs from epoll:\nstdoutFd, stderrFd, stdinFd\nErase from _cgiToClient"]
    CleanPipes --> Kill["CGIHandler::checkTimeout() internally:\nkill(_pid, SIGTERM)\nwaitpid(_pid, WNOHANG)"]
    
    Kill --> WhichState{"Which state\nwas the client in?"}
    WhichState -->|"STATE_CGI_RUNNING\n(before headers sent)"| Error504["Build clean 504 Gateway Timeout page"]
    WhichState -->|"STATE_CGI_STREAMING\n(mid-stream)"| TermChunk["Append terminal chunk 0\\r\\n\\r\\n\n(can't send 504 — headers already sent!)"]
    
    Error504 --> Resume["_setEpollEvents(clientFd, EPOLLOUT)"]
    TermChunk --> Resume
    Resume --> Done["Client receives error response"]
    
    Check -->|"No"| IdleCheck{"now - lastActivity\n> 60 seconds?"}
    IdleCheck -->|"Yes"| Disc["_handleDisconnect(fdToClose)"]
    IdleCheck -->|"No"| Skip["Skip (client is active)"]
```

> [!WARNING]
> **Iterator invalidation:** When disconnecting an idle client inside the timeout loop, the FD must be saved, the iterator advanced with `++it`, and only then `_handleDisconnect()` called. Deleting the current iterator first would cause a segmentation fault on `++it`.

---

## 8. Module Integration

This diagram shows exactly where the core server module calls the HTTP and CGI modules, and what data flows between them:

```mermaid
flowchart LR
    subgraph CoreServer ["Core Server — srcs/server/"]
        EL["EventLoop"]
        Sock["Socket"]
        Cli["Client struct"]
    end

    subgraph HTTPModule ["HTTP Module — srcs/http/"]
        Req["Request\n(parser)"]
        Res["Response\n(builder)"]
        Rtr["Router\n(dispatch)"]
    end

    subgraph CGIModule ["CGI & Config — srcs/config/ + srcs/cgi/"]
        CP["ConfigParser"]
        SC["ServerConfig"]
        CGI["CGIHandler"]
    end

    %% Core Server calls HTTP Module
    EL -->|"client.request.feed(buf, bytes)"| Req
    EL -->|"client.request.isComplete()"| Req
    EL -->|"Router::resolveVirtualHost(req, port, configs)"| Rtr
    EL -->|"Router::handleRequest(req, res, locations)"| Rtr
    EL -->|"client.response.isCgi()"| Res
    EL -->|"client.response.getNextChunk()"| Res
    EL -->|"client.request.isKeepAlive()"| Req

    %% Core Server calls CGI Module
    EL -->|"client.cgi.startFromRequest(...)"| CGI
    EL -->|"client.cgi.onStdinReady/onStdoutReady/onStderrReady()"| CGI
    EL -->|"client.cgi.checkTimeout()"| CGI
    EL -->|"client.cgi.getState()"| CGI
    EL -->|"_configs vector of ServerConfig"| SC

    %% HTTP Module uses Config data
    Rtr -->|"Uses LocationConfig\nfor route matching"| SC
```

### Integration Contracts

| When the EventLoop calls... | Expected return | Purpose |
|---|---|---|
| `request.feed(data)` | Parser accumulates bytes internally | Streaming request parsing |
| `request.isComplete()` | `true` when full HTTP request is parsed | Triggers dispatch |
| `Router::resolveVirtualHost()` | Pointer to matching `ServerConfig` by `Host` header + port | Virtual hosting |
| `Router::handleRequest()` | `Response` object populated (static file, error, or CGI flag) | Request routing |
| `response.isCgi()` | `true` if route matched a CGI extension | CGI vs static branching |
| `response.getNextChunk()` | Next piece of serialized HTTP response (headers + body chunk) | Streaming response |
| `cgi.startFromRequest()` | `true` if fork+execve succeeded; pipe FDs are now valid | CGI process creation |
| `cgi.onStdoutReady()` | Reads available data from CGI stdout pipe (non-blocking) | CGI output collection |
| `cgi.getState()` | `CGI_DONE` or `CGI_ERROR` when process finished | CGI lifecycle tracking |

---

## 9. File Descriptor Ownership & Leak Prevention

> [!IMPORTANT]
> File descriptor leaks are a common failure point in long-running servers. Every FD type has exactly one owner responsible for closing it.

```
 FD Type              Owner              Created At                    Closed At
 ─────────────────────────────────────────────────────────────────────────────────
 _epollFd             EventLoop          EventLoop() constructor       ~EventLoop() destructor

 listenFd             Socket             Socket::_createSocket()       Socket::~Socket()

 clientFd             EventLoop          accept() in _handleAccept     _handleDisconnect()
                      (_clients map)                                   OR ~EventLoop()

 cgiStdinFd           CGIHandler         pipe() in CGIHandler::start   CGIHandler::onStdinReady()
 cgiStdoutFd                                                           when EOF detected
 cgiStderrFd                                                           OR CGIHandler::cleanup()
                                                                       OR CGIHandler::checkTimeout()
```

### Leak Prevention Guarantees

| Scenario | What Happens |
|----------|-------------|
| Client sends `FIN` (clean close) | `recv()` returns `0` → `_handleDisconnect(fd)` → `close(fd)` + erase from maps |
| Client crashes / network error | `EPOLLERR` or `EPOLLHUP` event → `_handleDisconnect(fd)` |
| Client goes silent (half-open) | `_checkTimeouts()` detects `now - lastActivity > 60s` → `_handleDisconnect(fd)` |
| CGI hangs forever | `cgi.checkTimeout()` kills child, closes pipes → `buildErrorPage(504)` |
| Client disconnects during CGI | `_handleDisconnect()` checks `STATE_CGI_RUNNING` → removes all pipe FDs from epoll and `_cgiToClient` |
| Server receives `SIGINT` (Ctrl+C) | `g_running = 0` → loop exits → `~EventLoop()` closes all remaining client FDs + `_epollFd` |
| Socket setup fails mid-way | Each `Socket::_*()` method returns false → `setup()` short-circuits → destructor closes `_fd` (RAII) |

---

## 10. Signal Handling & Graceful Shutdown

```mermaid
flowchart LR
    subgraph Signals ["Signals Handled"]
        SIGPIPE["SIGPIPE\n→ SIG_IGN"]
        SIGINT["SIGINT (Ctrl+C)\n→ signalHandler"]
        SIGQUIT["SIGQUIT (Ctrl+\\)\n→ signalHandler"]
    end

    subgraph Handler ["signalHandler()"]
        Set["g_running = 0\n(volatile sig_atomic_t)"]
    end

    subgraph Loop ["EventLoop::run()"]
        Check["while (_running && g_running)\nepoll_wait() returns EINTR"]
        Exit["Loop exits → ~EventLoop() cleanup"]
    end

    SIGPIPE -.->|"Ignored: prevents crash\nwhen send() to closed client"| Handler
    SIGINT --> Set
    SIGQUIT --> Set
    Set --> Check
    Check --> Exit
```

**Why `SIGPIPE` is ignored:** If a client disconnects while the server is `send()`ing data, the kernel sends `SIGPIPE` to the process. The default action is to **terminate the process**. By ignoring it, `send()` simply returns `-1` with `errno == EPIPE`, which `_handleWrite()` catches and handles gracefully via `_handleDisconnect()`.

---

## 11. Reference Tables

### Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `EPOLL_TIMEOUT_MS` | 1000 | `epoll_wait` wakes every 1s even without events (for timeout checks) |
| `CLIENT_TIMEOUT_SEC` | 60 | Close clients idle for 60 seconds |
| `READ_BUFFER_SIZE` | 8192 | Stack buffer for `recv()` — one read per event cycle |
| `EPOLL_SIZE` | 1024 | Hint to `epoll_create()` (ignored by modern kernels) |
| `MAX_EVENTS` | 1024 | Max events returned per `epoll_wait()` call |
| Listen backlog | 128 | Max pending TCP handshakes in kernel queue |

### Internal Data Structures

| Map | Key → Value | Purpose |
|-----|------------|---------|
| `_clients` | `clientFd → Client` | Tracks all active connections and their state |
| `_listenPorts` | `listenFd → port` | Identifies which FDs are listeners (for `_handleAccept`) |
| `_cgiToClient` | `pipeFd → clientFd` | Maps CGI pipe FDs back to their owning client |

### Event Dispatch Table

| Event Flag | Meaning | Handler |
|-----------|---------|---------|
| `EPOLLIN` on listener | New connection pending | `_handleAccept()` |
| `EPOLLIN` on client | Data ready to read | `_handleRead()` |
| `EPOLLOUT` on client | Socket ready for writing | `_handleWrite()` |
| `EPOLLERR \| EPOLLHUP` | Connection error or hangup | `_handleDisconnect()` |
| `EPOLLIN` on CGI pipe | CGI stdout/stderr has data | `_handleCgiReady()` |
| `EPOLLOUT` on CGI pipe | CGI stdin ready for write | `_handleCgiReady()` |

### Function Reference

| Function | Responsibility |
|----------|---------------|
| `_handleAccept()` | Drain accept queue, set non-blocking, register in epoll |
| `_handleRead()` | Single recv(), feed parser, dispatch when complete |
| `_handleWrite()` | Single send(), delegate to reload/keepalive helpers |
| `_reloadWriteBuffer()` | CGI chunk streaming & file streaming buffer refill |
| `_handleKeepAlive()` | Reset client for next request or disconnect |
| `_handleCgiReady()` | Route pipe events, manage CGI state machine |
| `_handleDisconnect()` | Clean up CGI pipes + client FD + maps |
| `_spawnCgi()` | Fork/exec, register pipes in epoll |
| `_checkTimeouts()` | Kill zombie CGIs, disconnect idle clients |
