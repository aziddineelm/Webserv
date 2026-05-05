# Webserv — AI Agent Context

## Project
HTTP/1.1 web server in **C++98**. 42 school project. 3-person team.

## Hard Constraints
- **C++98 only** — no `auto`, `nullptr`, range-for, lambdas, `std::unique_ptr`, `constexpr`
- Compile with: `c++ -Wall -Wextra -Werror -std=c++98`
- No external libraries, no Boost
- All sockets must be **non-blocking** (`fcntl` + `O_NONBLOCK`)
- Only **1 poll()/epoll()** call for ALL I/O — never read/write without readiness check
- Never check `errno` after read/write to adjust behavior
- `fork()` only for CGI execution

## Team Structure (Don't Touch Other People's Folders)
| Person | Folder | Role |
|--------|--------|------|
| Person A | `srcs/server/` | Sockets, event loop, connections |
| Person B | `srcs/http/` | HTTP parsing, routing, responses |
| Person C | `srcs/config/`, `srcs/cgi/` | Config parser, CGI handler |
| Shared  | `srcs/utils/`, `Makefile`, `main.cpp` | Utilities, build |

## Code Style
- Orthodox Canonical Form for classes (default ctor, copy ctor, assignment op, destructor)
- RAII for file descriptors (close in destructor)
- Header + source in same folder (not a separate `includes/` dir)
- No `using namespace std;`

## When I Ask for Code
- Always C++98
- Always handle `EAGAIN` on socket operations
- Always `close()` FDs on error paths
- One function at a time, not entire classes
