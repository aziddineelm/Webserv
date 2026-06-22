# CGI Support Overview

This project includes basic scaffolding for CGI support. The current
stubs provide structure and examples for implementing a production-
ready CGI subsystem.

Files created:

- `srcs/cgi/CGIHandler.hpp` / `.cpp` — high-level handler that runs a
  CGI script and collects output.
- `srcs/cgi/ProcessSpawner.hpp` / `.cpp` — low-level process and pipe
  utilities (fork/exec, pipe setup, file descriptor handling).
- `srcs/cgi/EnvBuilder.hpp` / `.cpp` — helpers to construct CGI
  environment variables from request and server metadata.
- `srcs/cgi/TempFile.hpp` / `.cpp` — helper to create temporary files
  for request bodies.
- `srcs/http/CGIResponseParser.hpp` / `.cpp` — parse CGI stdout into
  headers and body.
- `www/cgi-bin/hello.py` — example CGI script for manual testing.
- `www/cgi-bin/echo.py` — echoes POST body for integration testing.
- `www/cgi-bin/status.py` — returns a `Status:` header for parser tests.

Next steps to implement full CGI:

- Implement `ProcessSpawner::spawn` with proper `fork()`/`execve()` and
  pipe setup; ensure non-blocking IO or use poll/select to avoid
  deadlocks.
- Implement `CGIHandler::run` to use `ProcessSpawner`, `EnvBuilder`,
  `TempFile` as needed, and enforce timeouts and output limits.
- Populate CGI-related fields in `ServerConfig` via the config parser
  (`cgi_extensions`, `cgi_map`, `cgi_path`).
- Add tests in `tests/cgi/` and example scripts under `www/cgi-bin/`.
- Run the bundled CGI checks with `make cgi-test` (runs hello/echo/status).

Security notes
--------------

- CGI runs arbitrary code. Run with least privileges, validate script
  paths, and avoid exposing sensitive environment variables.
- Enforce timeouts, CPU and memory limits where possible.

