# Webserv — AI Agent Context

## Project
HTTP/1.1 web server in **C++98**. 42 school project. 3-person team.
see workflow.md
/home/aysadeq/Desktop/Webserv/README/webserv_workflow.md

## Hard Constraints
- **C++98 only** — no `auto`, `nullptr`, range-for, lambdas, `std::unique_ptr`, `constexpr`
- Compile with: `c++ -Wall -Wextra -Werror -std=c++98`
- No external libraries, no Boost
- All sockets must be **non-blocking** (`fcntl` + `O_NONBLOCK`)
- Only **1 poll()/epoll()** call for ALL I/O — never read/write without readiness check
- Checking `errno` after read/write is strictly **forbidden**
- `fork()` only for CGI execution

## Allowed External Functions ONLY
/home/aysadeq/Desktop/Webserv/en.Webserv.txt
execve, pipe, strerror, gai_strerror, errno, dup, dup2, fork, socketpair,
htons, htonl, ntohs, ntohl, select, poll, epoll (epoll_create, epoll_ctl,
epoll_wait), kqueue (kqueue, kevent), socket, accept, listen, send, recv,
chdir, bind, connect, getaddrinfo, freeaddrinfo, setsockopt, getsockname,
getprotobyname, fcntl, close, read, write, waitpid, kill, signal, access,
stat, open, opendir, readdir, closedir

**NOT allowed:** inet_ntoa, inet_addr, inet_pton, inet_ntop, etc.

## Team Structure (Don't Touch Other People's Folders)
| Person | Folder | Role |
|--------|--------|------|
| Person A (Ayman) | `srcs/server/` | Sockets, event loop, connections |
| Person B | `srcs/http/` | HTTP parsing, routing, responses |
| Person C | `srcs/config/`, `srcs/cgi/` | Config parser, CGI handler |
| Shared  | `srcs/utils/`, `Makefile`, `main.cpp` | Utilities, build |

## Current State — Phase 1 COMPLETE
- Socket class: wraps socket()→setsockopt()→bind()→listen()→fcntl()
- Server class: manages multiple Sockets, simple accept loop
- Signal handling: SIGPIPE ignored, SIGINT/SIGQUIT for clean shutdown
- Tested: both ports work, SO_REUSEADDR works, no FD leaks

## Phase 2 — IN PROGRESS (Event Loop)
- Replace Server::run() simple loop with poll()-based event loop
- Add Client struct (readBuffer, writeBuffer, state, timestamps)
- Track FD types: LISTEN, CLIENT, CGI_PIPE
- Add connection timeout checking
- Must support adding/removing FDs dynamically (for accept and close)

## Key Design Decisions Made
- Bool returns for error handling (not exceptions) — entire team agreed
- Socket is non-copyable (prevents double-close bugs)
- Server owns Socket* pointers (not objects) because Socket is non-copyable
- volatile sig_atomic_t g_running — global signal flag
- RAII: Socket destructor closes FD, Server destructor deletes Sockets


## Code Style
- RAII for file descriptors (close in destructor)
- Header + source in same folder
- No `using namespace std;`
- Every system call return value checked

## When I Ask for Code
- Always C++98
- Always handle EAGAIN on socket operations
- Always close() FDs on error paths
- Use only allowed external functions
