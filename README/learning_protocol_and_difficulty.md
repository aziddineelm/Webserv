# Webserv — Learning Protocol Map & Difficulty Comparison

## Part 1: Learning Protocol Map (Concepts Only)

> [!IMPORTANT]
> This map covers **what each person needs to understand conceptually** — not how to implement it. The goal: everyone can explain and reason about every part during evaluation.

---

### Phase 0 — Shared Conceptual Foundation (All 3, Before Splitting)

Every team member must understand these concepts before anyone writes a single line of code:

| Concept | What to Understand |
|---------|-------------------|
| **Client-Server Model** | What is a client? What is a server? How do they find each other? What is a port? |
| **TCP/IP Basics** | What is a TCP connection? What does "reliable, ordered, byte-stream" mean? What is a 3-way handshake? |
| **What is a Socket?** | A socket is a file descriptor representing one end of a network connection. It's just a number the OS gives you. |
| **HTTP Request/Response Lifecycle** | Client opens connection → sends request → server processes → sends response → (maybe) closes connection. One full cycle. |
| **HTTP Message Format** | Request line + headers + blank line + body. Response: status line + headers + blank line + body. |
| **Blocking vs Non-Blocking I/O** | Blocking = your program freezes waiting for data. Non-blocking = it checks and moves on. Why blocking kills a multi-client server. |
| **I/O Multiplexing Concept** | Instead of one thread per client, one loop watches ALL connections. "Tell me which sockets have data ready." That's `poll()`/`epoll()`. |
| **What is CGI?** | Server runs an external program, feeds it the request, reads its output as the response. It's just stdin/stdout through a child process. |
| **Process Concepts** | `fork()` = clone yourself. `execve()` = replace yourself with another program. `pipe()` = a one-way data tube between processes. |

```
                    ┌─────────────────────────────┐
                    │   SHARED FOUNDATION         │
                    │                             │
                    │  TCP/IP · HTTP Lifecycle    │
                    │  Sockets · Blocking vs Non  │
                    │  I/O Multiplexing · CGI idea│
                    │  fork/exec/pipe concepts    │
                    └──────────┬──────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
        ┌───────────┐   ┌───────────┐   ┌───────────┐
        │ Person A  │   │ Person B  │   │ Person C  │
        │ Deep Dive │   │ Deep Dive │   │ Deep Dive │
        └───────────┘   └───────────┘   └───────────┘
```

---

### Person A — Core Server & Networking (Deep Concepts)

These are the concepts Person A must **deeply** understand:

| # | Concept | What to Understand |
|---|---------|-------------------|
| 1 | **Socket Lifecycle** | `socket()` → `bind()` → `listen()` → `accept()` → `recv()`/`send()` → `close()`. Each step's purpose. Why this specific order. |
| 2 | **Address Binding** | What does binding to `0.0.0.0:8080` mean vs `127.0.0.1:8080`? What is `SO_REUSEADDR` and why you need it? |
| 3 | **Listening & Backlog** | What does the backlog parameter in `listen()` do? What happens when the backlog is full? |
| 4 | **Accept & New FDs** | `accept()` returns a NEW file descriptor for each client. The listening socket stays listening. Two different FDs, two different purposes. |
| 5 | **Non-Blocking Mode** | `fcntl(fd, F_SETFL, O_NONBLOCK)` — what changes? `recv()` returns `-1` with `EAGAIN` instead of freezing. You must handle this. |
| 6 | **I/O Multiplexing (poll/epoll)** | One call watches many FDs. `POLLIN` = data ready to read. `POLLOUT` = ready to write. `POLLHUP` = client gone. The event loop pattern: poll → iterate events → act → poll again. |
| 7 | **The Event Loop Pattern** | `while(true) { poll() → for each ready FD: if listening FD → accept(); if client FD readable → read(); if client FD writable → write(); }` |
| 8 | **Connection State Machine** | Each client connection has a state: READING_REQUEST → PROCESSING → WRITING_RESPONSE → DONE. The event loop drives transitions. |
| 9 | **Partial Reads & Writes** | `recv()` might return 5 bytes when 1000 were sent. `send()` might send 100 of 5000. You must buffer and resume. |
| 10 | **Timeouts** | Idle clients must be dropped. Track last activity time per connection. Check in the event loop. |
| 11 | **File Descriptor Leaks** | Every `socket()`/`accept()` must have a matching `close()`. If you forget, you run out of FDs and the server dies. |
| 12 | **Signals** | `SIGPIPE` when writing to a closed socket — must ignore or handle. `SIGINT` for graceful shutdown. |

---

### Person B — HTTP Protocol & Request/Response (Deep Concepts)

| # | Concept | What to Understand |
|---|---------|-------------------|
| 1 | **HTTP Request Structure** | `METHOD SP URI SP VERSION CRLF` then `Header: Value CRLF` repeated, then empty `CRLF`, then optional body. Every byte matters. |
| 2 | **HTTP Methods Semantics** | `GET` = retrieve (no body expected). `POST` = submit data (body present). `DELETE` = remove resource. Idempotency concept. |
| 3 | **Headers That Matter** | `Host` (required in HTTP/1.1, selects virtual host), `Content-Length` (body size), `Transfer-Encoding: chunked`, `Connection: keep-alive/close`, `Content-Type` |
| 4 | **Chunked Transfer Encoding** | Body comes in chunks: `SIZE_IN_HEX CRLF DATA CRLF` repeated, terminated by `0 CRLF CRLF`. Why it exists: sender doesn't know total size upfront. |
| 5 | **HTTP Response Structure** | `VERSION SP STATUS_CODE SP REASON CRLF` then headers, then blank line, then body. Must set `Content-Length` and `Content-Type`. |
| 6 | **Status Codes (Families)** | `2xx` = success, `3xx` = redirect, `4xx` = client error, `5xx` = server error. Know the important ones: 200, 201, 204, 301, 400, 403, 404, 405, 413, 500. |
| 7 | **MIME Types** | File extension → Content-Type mapping. `.html` → `text/html`, `.css` → `text/css`, `.jpg` → `image/jpeg`. Browser needs this to render correctly. |
| 8 | **URI & Routing** | URI path maps to a filesystem path via config `root`. `/images/cat.jpg` with `root /var/www` → `/var/www/images/cat.jpg`. Location blocks refine this. |
| 9 | **Directory Listing vs Index** | Request for `/docs/` → if `index.html` exists, serve it. If not and `autoindex on`, generate an HTML listing. If neither, 403 or 404. |
| 10 | **HTTP Redirections** | `301 Moved Permanently` / `302 Found` — response has `Location` header with the new URL. No body needed. Browser follows automatically. |
| 11 | **File Upload (multipart/form-data)** | POST body has a boundary string. Each part has sub-headers (`Content-Disposition: form-data; name="file"; filename="photo.jpg"`). Parse boundary-delimited sections. |
| 12 | **Error Pages** | Every error code should serve an HTML page. Default built-in ones + custom ones from config. Never send a raw status code without a body to a browser. |

---

### Person C — Configuration Parser & CGI (Deep Concepts)

| # | Concept | What to Understand |
|---|---------|-------------------|
| 1 | **Configuration as Data** | The config file is a data structure in text form. Parsing = text → structured C++ objects your program queries at runtime. |
| 2 | **Block-Scoped Config (NGINX model)** | `server { ... }` contains server-level settings. `location /path { ... }` nested inside overrides/extends them. It's like scope in programming — inner overrides outer. |
| 3 | **Directive Types** | Simple: `listen 8080;` (key-value). Block: `location /path { ... }` (scoped container). Each directive has validation rules. |
| 4 | **Config Inheritance & Defaults** | If `location` doesn't specify `root`, it inherits from the `server` block. If `server` doesn't specify something, use program defaults. |
| 5 | **Multiple Server Blocks** | Same port can have multiple servers distinguished by `server_name`. Different ports = different sockets. Config must express both. |
| 6 | **Location Matching** | Request URI `/docs/file.txt` matches `location /docs` but not `location /api`. Most specific (longest prefix) wins. |
| 7 | **Config Validation** | Port must be a number 1-65535. `root` must exist. `allowed_methods` must be valid HTTP methods. Fail early with clear error messages. |
| 8 | **What CGI Actually Is** | Web server receives request → "I can't handle this myself" → launches an external program → feeds it request data → reads program's output → sends output as HTTP response. |
| 9 | **CGI Environment Variables** | The contract between server and CGI script. `REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_TYPE`, `CONTENT_LENGTH`, `SCRIPT_NAME`, `PATH_INFO`, `SERVER_PORT` — this IS the CGI spec. |
| 10 | **fork() + execve() Pattern** | `fork()` = the process clones itself. Parent and child diverge. Child calls `execve()` to become the CGI script. Parent waits for output. |
| 11 | **Pipes for IPC** | `pipe()` creates two FDs: read-end and write-end. Parent writes request body to child's stdin via pipe. Parent reads child's stdout via another pipe. `dup2()` redirects stdin/stdout. |
| 12 | **CGI Output Parsing** | CGI script outputs headers first (`Content-Type: text/html`), blank line, then body. Server must parse these and merge into the HTTP response. |
| 13 | **CGI Process Management** | Child can hang → need timeout + `kill()`. Child can crash → `waitpid()` detects exit status. Zombie processes = child died but parent didn't `waitpid()`. |

---

### Cross-Learning Map (What Each Person Must Learn From the Others)

```
┌──────────────────────────────────────────────────────────────┐
│                    CROSS-LEARNING MAP                        │
├──────────────┬──────────────────┬────────────────────────────┤
│              │ Must learn FROM  │ Must learn FROM            │
│              │ the other two    │ the other two              │
├──────────────┼──────────────────┼────────────────────────────┤
│              │                  │                            │
│  Person A    │ FROM B:          │ FROM C:                    │
│  (Network)   │ • HTTP message   │ • Config structure         │
│              │   format         │ • How server blocks map    │
│              │ • Status codes   │   to listening sockets     │
│              │   meaning        │ • CGI fork/pipe flow       │
│              │ • Routing logic  │   (conceptually)           │
│              │                  │                            │
├──────────────┼──────────────────┼────────────────────────────┤
│              │                  │                            │
│  Person B    │ FROM A:          │ FROM C:                    │
│  (HTTP)      │ • Event loop     │ • Config location block    │
│              │   flow           │   matching rules           │
│              │ • Why non-block  │ • CGI environment          │
│              │ • Partial read/  │   variables (what the      │
│              │   write concept  │   server must provide)     │
│              │ • Connection     │ • How CGI output becomes   │
│              │   state machine  │   an HTTP response         │
│              │                  │                            │
├──────────────┼──────────────────┼────────────────────────────┤
│              │                  │                            │
│  Person C    │ FROM A:          │ FROM B:                    │
│  (Config+CGI)│ • How config     │ • HTTP request format      │
│              │   drives socket  │   (what fields exist)      │
│              │   creation       │ • How routing uses          │
│              │ • How CGI pipes  │   location blocks          │
│              │   integrate with │ • Response building        │
│              │   the event loop │   (status + headers + body)│
│              │ • Non-blocking   │                            │
│              │   CGI I/O        │                            │
│              │                  │                            │
└──────────────┴──────────────────┴────────────────────────────┘
```

---

### Learning Protocol Timeline

| When | Activity | Who | Format |
|------|----------|-----|--------|
| **Week 1** | Study shared foundation concepts together | All 3 | Group study, whiteboard |
| **Week 2** | Person A teaches: "The Event Loop" (concept) | A → B,C | 30-min whiteboard session |
| **Week 2** | Person B teaches: "HTTP Message Anatomy" | B → A,C | 30-min whiteboard session |
| **Week 2** | Person C teaches: "Config Block Scoping" | C → A,B | 30-min whiteboard session |
| **Week 3** | Person A teaches: "Connection State Machine & Partial I/O" | A → B,C | 30-min session |
| **Week 3** | Person B teaches: "Routing & Status Codes" | B → A,C | 30-min session |
| **Week 3** | Person C teaches: "fork/execve/pipe for CGI" | C → A,B | 30-min session |
| **Week 4** | Pair integration — everyone reads everyone's code | All 3 | Code review sessions |
| **Week 5** | Mock evaluation: grill each other | All 3 | Simulated defense |

---

## Part 2: Difficulty Comparison

### Overall Difficulty Rating

| Role | Difficulty | Score /10 |
|------|-----------|-----------|
| **Person A** — Core Server & Networking | 🔴 **Hardest** | **9/10** |
| **Person B** — HTTP Protocol & Request/Response | 🟡 **Medium-Hard** | **7/10** |
| **Person C** — Config Parser & CGI | 🟢 **Medium** | **6/10** |

---

### Detailed Breakdown: Person A (Difficulty 9/10) 🔴

| Factor | Rating | Why |
|--------|--------|-----|
| **Conceptual Difficulty** | 🔴 Very High | Non-blocking I/O, event-driven programming, and state machines are genuinely hard mental models. Most people think sequentially —  this requires thinking in events. |
| **Debugging Difficulty** | 🔴 Very High | Race-condition-like bugs, FD leaks, partial reads that work on localhost but fail under load. Bugs are intermittent and hard to reproduce. |
| **Criticality** | 🔴 Maximum | If the event loop is broken, **nothing works**. A and B and C all depend on this. It's the foundation of the entire server. |
| **Room for Error** | 🔴 Very Low | A single missed `close()` = FD leak = server dies after 1000 connections. A single blocking call = entire server freezes for all clients. |
| **Prior Exposure** | 🔴 Low | Most 42 students have never written socket code or used `poll()`. Completely new territory. |
| **Subject-Specific Pressure** | 🔴 Very High | "Your server must remain non-blocking at all times" + "use only 1 poll() for all I/O" + "never read/write without poll()" — strict rules, zero flexibility. |

**Why it's the hardest:** Person A is building the *engine* that everything else runs on. Event-driven, non-blocking programming is a paradigm shift from sequential code. The bugs are subtle (works with 1 client, crashes with 100), and the subject has absolute rules (one poll, always non-blocking, never read without readiness).

---

### Detailed Breakdown: Person B (Difficulty 7/10) 🟡

| Factor | Rating | Why |
|--------|--------|-----|
| **Conceptual Difficulty** | 🟡 Medium | HTTP is a text protocol — it's complex but logical. Parsing rules are well-documented in RFCs. |
| **Volume of Work** | 🔴 High | The most *features* to implement: parsing, routing, response building, static serving, uploads, DELETE, error pages, directory listing, redirects. |
| **Edge Cases** | 🟡 Many | Malformed requests, missing headers, URL encoding, chunked bodies, multipart boundaries, large files, MIME detection. |
| **Debugging Difficulty** | 🟡 Medium | Easier than A — you can `curl -v` and see exactly what's wrong. Inputs and outputs are readable text. |
| **Prior Exposure** | 🟢 Moderate | Most people have seen HTTP from the client side. Parsing text is familiar from previous 42 projects. |
| **Subject-Specific Pressure** | 🟡 Medium | "HTTP response status codes must be accurate" + "serve a fully static website" + "upload files" — many requirements but each is clearly defined. |

**Why it's medium-hard:** The difficulty isn't in any single concept — it's in the **sheer breadth**. Person B has the most features to build and the most edge cases to handle. But each individual feature is understandable and testable in isolation. `curl -v` makes debugging much easier compared to A's socket-level mysteries.

---

### Detailed Breakdown: Person C (Difficulty 6/10) 🟢

| Factor | Rating | Why |
|--------|--------|-----|
| **Conceptual Difficulty** | 🟡 Medium | Config parsing is string manipulation. CGI uses `fork()`/`execve()` which students saw in previous projects (pipex/minishell). |
| **Volume of Work** | 🟢 Lower | Two main deliverables: config parser + CGI handler. Fewer features than B. |
| **Prior Knowledge Reuse** | 🟢 High | `fork()`, `execve()`, `pipe()`, `dup2()`, `waitpid()` — all covered in **pipex** and **minishell**. This is revisiting familiar ground with a new context. |
| **Debugging Difficulty** | 🟡 Medium | Config parser: easy to test (parse → print → verify). CGI: harder — child process issues, pipe deadlocks, zombie processes. |
| **Integration Complexity** | 🟡 Medium | CGI must integrate with A's event loop (non-blocking pipe reads). This is the hardest part of C's role and requires collaboration with A. |
| **Subject-Specific Pressure** | 🟢 Moderate | Config format is flexible ("take inspiration from NGINX"). CGI needs to work with at least one language. |

**Why it's the easiest:** Config parsing is fundamentally string parsing — challenging but conceptually familiar. CGI heavily reuses concepts from pipex/minishell (`fork`/`execve`/`pipe`/`dup2`). The hardest part is making CGI non-blocking within A's event loop, but that's a **collaboration** challenge more than an individual one.

---

### Difficulty Comparison At a Glance

```
Person A (Server/Network)   ██████████████████░░  9/10  🔴 Hardest
Person B (HTTP Protocol)    ██████████████░░░░░░  7/10  🟡 Medium-Hard
Person C (Config + CGI)     ████████████░░░░░░░░  6/10  🟢 Medium
```

### Difficulty by Dimension

| Dimension | Person A | Person B | Person C |
|-----------|----------|----------|----------|
| **Conceptual complexity** | 🔴🔴🔴 | 🟡🟡 | 🟡 |
| **Volume of features** | 🟡 | 🔴🔴🔴 | 🟢 |
| **Debugging pain** | 🔴🔴🔴 | 🟡 | 🟡 |
| **Prior 42 experience helps** | 🔴 (new) | 🟡 (some) | 🟢 (pipex/minishell) |
| **Impact if broken** | 🔴🔴🔴 (kills everything) | 🔴🔴 (no HTTP = no server) | 🟡 (server works, just no CGI/dynamic config) |
| **Edge cases** | 🟡🟡 | 🔴🔴🔴 | 🟡 |
| **Integration burden** | 🔴🔴 (integrates with both) | 🟡 (integrates with A and C) | 🟡 (integrates with A and B) |

---

### Difficulty Balancing Recommendations

> [!TIP]
> The workload is naturally unequal. Here's how to balance it:

| Imbalance | How to Fix |
|-----------|-----------|
| Person A has the hardest work | Person B and C should help A with **testing and stress testing** in weeks 4-5 |
| Person B has the most features | Person C (lower volume) should help B with **error pages and directory listing** after finishing config |
| Person C finishes earliest | C should take ownership of **testing infrastructure** — writing test scripts, setting up `siege`, building the test website |
| A is the bottleneck | Start A's echo server in Week 1 *with the whole team*. If A gets stuck, the entire project stalls |

> [!IMPORTANT]
> **Assign your strongest systems programmer to Person A.** The event loop is the most critical and hardest component. If Person A struggles, consider having the whole team pair-program the event loop together before splitting.
