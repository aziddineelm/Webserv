# Webserv — Team Workflow & Task Split (3 People)

## Project Overview

Build a **fully functional HTTP/1.1 web server** in C++98 that handles multiple clients concurrently using non-blocking I/O. The server must support GET, POST, DELETE, CGI execution, virtual hosting, and be configurable via an NGINX-inspired config file.

---

## Team Roles

| Role | Person | Focus Area |
|------|--------|------------|
| **Person A** | _______  | **Core Server & Networking** |
| **Person B** | _______  | **HTTP Protocol & Request/Response** |
| **Person C** | _______  | **Configuration Parser & CGI** |

---

## Phase 0 — Shared Foundation (All 3, Week 1)

> [!IMPORTANT]
> Everyone should complete this phase together before splitting. This ensures a shared understanding of the architecture.

### Tasks
- [ ] Agree on **project structure** (directory layout, naming conventions)
- [ ] Define **shared interfaces/classes** (see Architecture below)
- [ ] Set up the **Makefile** (`all`, `clean`, `fclean`, `re`, `-Wall -Wextra -Werror -std=c++98`)
- [ ] Create a basic `.gitignore`
- [ ] Write the **README.md** skeleton

### What Everyone Should Learn
| Topic | Why | Resource |
|-------|-----|----------|
| TCP/IP basics | Understand what a socket is | Beej's Guide to Network Programming |
| HTTP/1.1 overview | Understand request/response lifecycle | [RFC 2616](https://www.rfc-editor.org/rfc/rfc2616) (skim sections 4–6, 10, 14) |
| `poll()` / `epoll()` concept | Core I/O multiplexing mechanism | `man 2 poll`, `man 7 epoll` |
| Non-blocking I/O | Why `fcntl(fd, F_SETFL, O_NONBLOCK)` matters | Blog posts + man pages |
| NGINX config basics | Inspiration for your config format | NGINX beginner's guide |

### Proposed Directory Structure
```
Webserv/
├── Makefile
├── README.md
├── config/
│   └── default.conf
├── srcs/
│   ├── main.cpp
│   ├── server/
│   │   ├── Server.cpp / .hpp        # Person A
│   │   ├── Socket.cpp / .hpp        # Person A
│   │   └── EventLoop.cpp / .hpp     # Person A
│   ├── http/
│   │   ├── Request.cpp / .hpp       # Person B
│   │   ├── Response.cpp / .hpp      # Person B
│   │   ├── Router.cpp / .hpp        # Person B
│   │   └── ErrorPages.cpp / .hpp    # Person B
│   ├── config/
│   │   ├── ConfigParser.cpp / .hpp  # Person C
│   │   └── ServerConfig.cpp / .hpp  # Person C
│   ├── cgi/
│   │   └── CgiHandler.cpp / .hpp    # Person C
│   └── utils/
│       └── Utils.cpp / .hpp         # Shared
└── www/                             # Test website files
    ├── index.html
    ├── error/
    │   ├── 404.html
    │   └── 500.html
    └── uploads/
```

---

## Person A — Core Server & Networking

### Responsibilities
Build the **engine** — the socket layer, the event loop, and connection management. Everything that makes the server accept, read, write, and close connections.

### Phase 1 — Socket & Listening (Week 1–2)
- [ ] Create `Socket` class: bind, listen, set non-blocking
- [ ] Support **multiple listening ports** (one socket per port)
- [ ] Handle `SO_REUSEADDR` and `SO_REUSEPORT`
- [ ] Test: server listens on configured ports, accepts `telnet` connections

### Phase 2 — Event Loop (Week 2–3)
- [ ] Implement the **single poll/epoll loop** for all I/O
- [ ] Register listening sockets → accept new clients
- [ ] Register client sockets → track read/write readiness
- [ ] Implement **connection timeout** (drop idle clients)
- [ ] Handle client disconnections gracefully (no crashes, no leaks)
- [ ] Test: multiple simultaneous `telnet` or `curl` connections

### Phase 3 — Integration (Week 3–4)
- [ ] When data is ready to read → pass raw data to **Person B**'s `Request` parser
- [ ] When response is ready → write **Person B**'s `Response` buffer to client
- [ ] Support **chunked writing** (large responses sent over multiple poll cycles)
- [ ] Integrate with **Person C**'s config (which ports to listen on, server blocks)

### Phase 4 — Hardening (Week 4–5)
- [ ] Stress test with `siege` or `ab` (Apache Bench)
- [ ] Ensure no file descriptor leaks
- [ ] Verify `client_max_body_size` enforcement (reject oversized requests)
- [ ] Test with a real browser (Chrome/Firefox)

### What Person A Should Learn

| Topic | Depth | Key Resources |
|-------|-------|---------------|
| `socket()`, `bind()`, `listen()`, `accept()` | Deep | `man 2 socket`, Beej's Guide |
| `poll()` or `epoll()` | Deep | `man 2 poll`, `man 7 epoll` |
| `fcntl()` with `O_NONBLOCK` | Medium | `man 2 fcntl` |
| `send()` / `recv()` | Deep | `man 2 send`, `man 2 recv` |
| `setsockopt()` | Medium | `SO_REUSEADDR`, `SO_REUSEPORT` |
| File descriptor management | Deep | Avoid leaks, `close()` on cleanup |
| Signals (`SIGPIPE`, `SIGINT`) | Medium | `signal()` or `sigaction()` |
| C++98 class design (OOP, RAII) | Medium | Orthodox Canonical Form |

> [!TIP]
> Start with a **minimal echo server** that accepts a connection, reads data, and echoes it back. Then add `poll()`. Then add multi-client support.

---

## Person B — HTTP Protocol & Request/Response

### Responsibilities
Parse raw HTTP requests into structured objects, route them to the right handler, build proper HTTP responses, and serve static files.

### Phase 1 — Request Parsing (Week 1–2)
- [ ] Parse **request line**: `GET /path HTTP/1.1`
- [ ] Parse **headers**: `Host`, `Content-Length`, `Content-Type`, `Transfer-Encoding`, `Connection`
- [ ] Parse **body**: regular body and chunked transfer encoding (`Transfer-Encoding: chunked`)
- [ ] Validate method is one of: `GET`, `POST`, `DELETE`
- [ ] Handle malformed requests → return `400 Bad Request`
- [ ] Test: manually craft raw HTTP requests via `telnet`

### Phase 2 — Response Building (Week 2–3)
- [ ] Build `Response` class: status line + headers + body
- [ ] Implement **status codes**: 200, 201, 204, 301, 302, 400, 403, 404, 405, 413, 500
- [ ] Set `Content-Type` based on file extension (MIME types: `.html`, `.css`, `.js`, `.jpg`, `.png`, etc.)
- [ ] Set `Content-Length` header
- [ ] Default and custom **error pages** (from config)

### Phase 3 — Routing & Static File Serving (Week 3–4)
- [ ] Implement `Router`: match request URI to a **location block** (from Person C's config)
- [ ] Serve **static files** from the configured `root` directory
- [ ] Handle **directory listing** (`autoindex on/off`)
- [ ] Serve **default index file** (e.g., `index.html`) for directory requests
- [ ] Handle **HTTP redirections** (301/302 from config)
- [ ] Enforce **allowed methods** per location (return `405` if method not allowed)

### Phase 4 — File Uploads & DELETE (Week 3–4)
- [ ] Handle `POST` requests with `multipart/form-data` → save uploaded files
- [ ] Handle `DELETE` requests → remove specified resource
- [ ] Respect `client_max_body_size` → return `413 Payload Too Large`

### Phase 5 — Virtual Hosting (Week 4–5)
- [ ] Route requests to the correct **server block** based on `Host` header and `server_name`
- [ ] Implement default server fallback
- [ ] Test with multiple `server_name` values on the same port

### What Person B Should Learn

| Topic | Depth | Key Resources |
|-------|-------|---------------|
| HTTP/1.1 request format | Deep | [RFC 2616 §4-6](https://www.rfc-editor.org/rfc/rfc2616) |
| HTTP status codes | Deep | [RFC 2616 §10](https://www.rfc-editor.org/rfc/rfc2616#section-10) |
| MIME types | Medium | `Content-Type` mapping |
| Chunked Transfer Encoding | Deep | [RFC 2616 §3.6.1](https://www.rfc-editor.org/rfc/rfc2616#section-3.6.1) |
| `multipart/form-data` | Medium | [RFC 2388](https://www.rfc-editor.org/rfc/rfc2388) |
| URL encoding / decoding | Medium | `%20`, query strings |
| File I/O in C++ | Medium | `std::ifstream`, `stat()`, `opendir()` |
| Virtual hosting | Medium | `Host` header, `server_name` matching |

> [!TIP]
> Use `curl -v` extensively — it shows you the exact request/response exchange. Also use browser DevTools Network tab to observe real HTTP traffic.

---

## Person C — Configuration Parser & CGI

### Responsibilities
Parse the NGINX-inspired configuration file and implement CGI execution for dynamic content (e.g., PHP, Python scripts).

### Phase 1 — Configuration Parser (Week 1–2)
- [ ] Design the **config file format** (NGINX-like `server {}` and `location {}` blocks)
- [ ] Parse into a `ServerConfig` struct/class containing:
  - `listen` (port)
  - `server_name` (hostname)
  - `root` (document root)
  - `index` (default file)
  - `error_page` (custom error pages)
  - `client_max_body_size`
  - `location` blocks with:
    - `allowed_methods`
    - `root` / `alias`
    - `autoindex` (on/off)
    - `index`
    - `return` (redirections)
    - `cgi_extension` / `cgi_path`
    - `upload_store`
- [ ] Handle **default values** for missing directives
- [ ] Handle **multiple server blocks**
- [ ] Validate config (error on invalid directives, missing required fields)
- [ ] Test: parse various config files, print parsed structure

### Example Config File
```nginx
server {
    listen 8080;
    server_name localhost;
    root /var/www/html;
    index index.html;
    client_max_body_size 10M;

    error_page 404 /error/404.html;
    error_page 500 /error/500.html;

    location / {
        allowed_methods GET POST;
        autoindex off;
    }

    location /upload {
        allowed_methods GET POST DELETE;
        upload_store /var/www/uploads;
    }

    location /cgi-bin {
        allowed_methods GET POST;
        cgi_extension .py;
        cgi_path /usr/bin/python3;
    }

    location /redirect {
        return 301 https://example.com;
    }
}

server {
    listen 8081;
    server_name api.localhost;
    root /var/www/api;
}
```

### Phase 2 — CGI Handler (Week 2–4)
- [ ] Implement `CgiHandler` class
- [ ] Detect CGI requests by **file extension** (e.g., `.py`, `.php`)
- [ ] Set up CGI **environment variables**:
  - `REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_TYPE`, `CONTENT_LENGTH`
  - `SCRIPT_NAME`, `PATH_INFO`, `PATH_TRANSLATED`
  - `SERVER_NAME`, `SERVER_PORT`, `SERVER_PROTOCOL`
  - `HTTP_*` (forwarded headers)
- [ ] Execute CGI via `fork()` + `execve()`
- [ ] Pipe request body to CGI's `stdin` (for POST)
- [ ] Read CGI's `stdout` for the response
- [ ] Parse CGI output headers (e.g., `Content-Type`, `Status`)
- [ ] Handle **CGI timeout** (kill process if it runs too long)
- [ ] Make CGI execution **non-blocking** (integrate with Person A's event loop)
- [ ] Handle chunked request bodies (un-chunk before sending to CGI)
- [ ] Test with a Python CGI script and/or PHP-CGI

### Phase 3 — Integration & Edge Cases (Week 4–5)
- [ ] Provide config data to Person A (ports, server blocks) and Person B (routes, locations)
- [ ] Test CGI with a real browser (form submission → PHP/Python processing)
- [ ] Ensure CGI child processes are properly reaped (`waitpid`)
- [ ] Handle CGI errors gracefully (return `500` on failure)

### What Person C Should Learn

| Topic | Depth | Key Resources |
|-------|-------|---------------|
| String parsing in C++ | Deep | `std::string`, `std::istringstream` |
| File I/O (`std::ifstream`) | Medium | Reading config files |
| `fork()` and `execve()` | Deep | `man 2 fork`, `man 2 execve` |
| `pipe()` and `dup2()` | Deep | `man 2 pipe`, `man 2 dup2` |
| `waitpid()` | Medium | `man 2 waitpid`, `WNOHANG` |
| CGI specification | Deep | [CGI/1.1 RFC 3875](https://www.rfc-editor.org/rfc/rfc3875) |
| Environment variables | Medium | `setenv()`, `environ` |
| NGINX config syntax | Medium | NGINX docs for inspiration |
| Process management | Medium | Zombie processes, `SIGCHLD` |

> [!TIP]
> Write a simple Python CGI test script early:
> ```python
> #!/usr/bin/env python3
> print("Content-Type: text/html\r\n\r\n")
> print("<h1>CGI Works!</h1>")
> ```
> This lets you test your CGI handler independently.

---

## Dependency Graph

```mermaid
graph TD
    A[Phase 0: Shared Foundation] --> B[Person A: Socket & Listen]
    A --> C[Person B: Request Parsing]
    A --> D[Person C: Config Parser]
    
    B --> E[Person A: Event Loop]
    D --> F[Person C: CGI Handler]
    
    E --> G[Integration: A ↔ B]
    C --> G
    D --> G
    
    G --> H[Integration: A ↔ C for CGI]
    F --> H
    
    H --> I[Full Integration & Testing]
    
    style A fill:#4a9eff,color:#fff
    style G fill:#ff6b6b,color:#fff
    style H fill:#ff6b6b,color:#fff
    style I fill:#51cf66,color:#fff
```

---

## Timeline (5–6 Weeks)

| Week | Person A | Person B | Person C |
|------|----------|----------|----------|
| **1** | Shared foundation + Echo server | Shared foundation + HTTP study | Shared foundation + Config format design |
| **2** | Sockets + Event loop (poll) | Request parser + Response builder | Config parser implementation |
| **3** | Multi-client handling + timeouts | Routing + Static file serving | CGI handler (fork/execve/pipe) |
| **4** | **Integration with B** (read→parse→respond→write) | File uploads + DELETE + error pages | **Integration with A** (non-blocking CGI) |
| **5** | Stress testing + FD leak checks | Virtual hosting + browser compat | CGI edge cases + config validation |
| **6** | **Full team integration, testing, README, eval prep** | | |

---

## Integration Checkpoints

> [!WARNING]
> These are critical sync points. The team must meet and integrate code at these milestones.

### Checkpoint 1 (End of Week 2)
- Person A: Event loop accepts connections, reads raw data
- Person B: Can parse a raw HTTP request string
- Person C: Can parse a config file into a structured object
- **Test**: A reads data → passes to B → B returns parsed request

### Checkpoint 2 (End of Week 3)
- Person A + B: Server receives request, serves a static HTML file
- Person C: CGI handler can execute a Python script standalone
- **Test**: Open `http://localhost:8080/` in a browser → see a web page

### Checkpoint 3 (End of Week 4)
- All three integrated: config drives server behavior, CGI works
- **Test**: Upload a file via browser form, execute a CGI script, see error pages

### Final (End of Week 5–6)
- Stress test, edge cases, browser compatibility, README complete
- **Test with evaluator scenarios**: multiple servers, large uploads, CGI timeouts, bad requests

---

## Testing Checklist

- [ ] `curl -v http://localhost:8080/` → 200 + index.html
- [ ] `curl -v http://localhost:8080/nonexistent` → 404 error page
- [ ] `curl -X POST -d "data" http://localhost:8080/upload` → file saved
- [ ] `curl -X DELETE http://localhost:8080/upload/file.txt` → file deleted
- [ ] `curl http://localhost:8080/cgi-bin/test.py` → CGI output
- [ ] `siege -c 100 -t 10s http://localhost:8080/` → no crashes
- [ ] Multiple server blocks on different ports
- [ ] `client_max_body_size` rejection (413)
- [ ] Chunked transfer encoding
- [ ] Browser renders pages correctly (Chrome + Firefox)
- [ ] No memory leaks (`valgrind`)
- [ ] No file descriptor leaks (`ls /proc/<pid>/fd`)

---

## Bonus (If Time Permits)

| Bonus Feature | Who Should Handle |
|---------------|-------------------|
| Cookies & Sessions | Person B (HTTP layer) |
| Multiple CGI support | Person C |
| HEAD, PUT, OPTIONS, TRACE | Person B |

