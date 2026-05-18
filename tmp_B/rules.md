# Webserv — AI Agent Context

## Project
HTTP/1.1 web server in **C++98**. 42 school project. 3-person team.

## General rules

• Your program must not crash under any circumstances (even if it runs out of memory) or terminate unexpectedly.
If this occurs, your project will be considered non-functional and your grade will
be 0.
• You must submit a Makefile that compiles your source files. It must not perform
unnecessary relinking.
• Your Makefile must at least contain the rules:
$(NAME), all, clean, fclean and re.
• Compile your code with c++ and the flags -Wall -Wextra -Werror
• Your code must comply with the C++ 98 standard and should still compile when
adding the flag -std=c++98.
• Make sure to leverage as many C++ features as possible (e.g., choose <cstring>
over <string.h>). You are allowed to use C functions, but always prefer their C++
versions if possible.
• Any external library and Boost libraries are forbidden.

## Team Structure (Don't Touch Other People's Folders)
| Person | Folder | Role |
|--------|--------|------|
| Person A | `srcs/server/` | Sockets, event loop, connections |
| Person B | `srcs/http/` | HTTP parsing, routing, responses |
| Person C | `srcs/config/`, `srcs/cgi/` | Config parser, CGI handler |
| Shared  | `srcs/utils/`, `Makefile`, `main.cpp` | Utilities, build |

## Code Style
- Orthodox Canonical Form for classes (default ctor, copy ctor, assignment op, destructor)
- Header + source in same folder (not a separate `includes/` dir)
- No `using namespace std;`
- ALWAYS FOLLOR "DRY"
- ALWAYS MAKE  SURE EVERYTHING IS READABLE AND CLEAN AND JUNIOR CODER.

## When I Ask for Code
- Always C++98
- Always Follow the RFC 2616 and RFC 2388
- Always Follow the Structer of the code
- Am Person B, So always work on the /srcs/http dir and files, DO NOT TOUCH OTHERS FILES.
