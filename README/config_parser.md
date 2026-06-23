# ConfigParser

Overview
--------

`ConfigParser` is responsible for reading the server configuration file,
producing tokens, and building `ServerConfig` objects representing each
`server` block and its nested `location` blocks.

Key responsibilities
--------------------

- Tokenize raw file input and remove comments/whitespace where appropriate.
- Parse `server` blocks and `location` blocks into `ServerConfig` and
  associated location descriptors.
- Validate semantic constraints (port conflicts, required directives,
  etc.) and raise `ConfigException` on error.

Usage
-----

Typical usage:

```cpp
ConfigParser parser("/path/to/config.conf");
parser.parse();
parser.validate();
std::vector<ServerConfig> servers = parser.getServers();
```

Error handling
--------------

`ConfigParser::ConfigException` is thrown on parse/validation failure and
contains a human-readable message accessible via `what()`.

Notes for maintainers
---------------------

- The header `srcs/config/ConfigParser.hpp` now includes Doxygen-style
  comments for members and methods. Keep those comments in sync with
  implementation changes.
- For changes to tokenization rules, update both the implementation and
  this document with examples of supported directives and edge cases.

Examples
--------

If you need an example config to test parsing, use an existing
`invalid_tests.conf` in `config/` as a starting point; it contains
several malformed examples useful for validating error handling.
