# Configuration Parser Module

This module is responsible for reading, tokenizing, parsing, and validating the NGINX-inspired configuration file for the Webserv project. It provides a structured representation of the server and routing settings to the rest of the application.

## Architecture

The parser pipeline is built around these core components:

*   **`ConfigParser`**: The main class that drives the parsing process. It reads the raw file, tokenizes it by handling whitespace and braces, and performs a recursive-descent style parse to build the configuration objects.
*   **`ServerConfig`**: Represents a single `server {}` block. Contains server-wide settings like the listening port, host, server names, root directory, max body size, and custom error pages.
*   **`LocationContext`**: Represents a `location {}` block nested inside a server. Contains route-specific rules such as allowed HTTP methods, aliases, autoindex, redirects, and CGI execution rules.

## Supported Directives

The parser supports all directives required by the 42 subject, plus some additions for CGI handling:

### Server Directives (`server {}` block)
*   `listen <port> | <host>:<port>`: The address and port to bind to.
*   `server_name <name1> [name2...]`: Virtual host names for this server.
*   `root <path>`: The default document root directory.
*   `index <file>`: The default file to serve for directory requests.
*   `error_page <code1> [code2...] <path>`: Custom error pages for specific HTTP status codes.
*   `client_max_body_size <size>[K|M|G]`: Maximum allowed size for client request bodies.

### Location Directives (`location <path> {}` block)
*   `allowed_methods <GET|POST|DELETE>`: Strict list of allowed HTTP methods for this route.
*   `root <path>`: Overrides the server root for this specific route.
*   `alias <path>`: Replaces the requested URI path with a specific directory path.
*   `autoindex <on|off>`: Enables or disables directory listing.
*   `index <file>`: Overrides the default index file for this route.
*   `return <code> <url>`: Configures an HTTP redirect.
*   `upload_store <path>`: Directory where uploaded files should be saved.
*   `cgi_extension <.ext>`: The file extension that triggers CGI execution (e.g., `.php`, `.py`).
*   `cgi_path <path>`: The path to the CGI interpreter executable.

## Usage Guide

To use the parser during the server initialization phase:

### 1. Initialization and Parsing
Create a `ConfigParser` instance with the path to the configuration file, and call `parse()`. This will read the file and build the internal structures.
```cpp
#include "ConfigParser.hpp"

try {
    ConfigParser parser("config/default.conf");
    parser.parse();
    
    // ...
} catch (const std::exception& e) {
    std::cerr << "Parse error: " << e.what() << std::endl;
}
```

### 2. Validation
After a successful parse, you MUST call `validate()`. This phase performs semantic checks that cannot be done during tokenization:
*   Ensures that every `listen` address/port combination is unique across all servers.
*   Verifies that the required `root` directive is present.
*   Checks that all custom `error_page` files actually exist on the disk.
```cpp
    parser.validate();
```

### 3. Retrieving the Configuration
Once parsed and validated, retrieve the vector of `ServerConfig` objects and pass them to Person A (Server Core) to open sockets, and Person B (Router) to handle HTTP requests.
```cpp
    std::vector<ServerConfig> servers = parser.getServers();
    
    for (size_t i = 0; i < servers.size(); ++i) {
        // Pass servers[i] to your initialization logic
    }
```

## Error Handling
The parser is extremely strict. Any syntax errors, unknown directives, missing arguments, missing semicolons, or invalid values will immediately throw a `ConfigParser::ConfigException` with a descriptive error message. Do not catch this silently; the server should refuse to start with an invalid configuration.
