#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "ServerConfig.hpp"
#include <exception>
#include <fstream>
#include <string>
#include <vector>

// Parses the web server configuration file and builds server objects.
// ConfigParser reads a configuration file, tokenizes its contents,
// and constructs a set of ServerConfig instances describing the
// configured servers and their locations. Use `parse()` to perform
// parsing and `getServers()` to retrieve the parsed configuration.
class ConfigParser {
private:
    // Parsed server configurations.
    // Populated after a successful call to `parse()`; contains one
    // ServerConfig per `server` block in the config file.
    std::vector<ServerConfig> _servers;

    // Path to the configuration file to parse.
    std::string _configFilePath;

    // Tokenized representation of the configuration file.
    // Tokens are produced by `tokenize()` and consumed by the
    // parsing helper methods.
    std::vector<std::string> _tokens;

    // Read the file and split it into meaningful tokens.
    // This method reads `file`, strips comments/whitespace as appropriate,
    // and fills `_tokens` with the resulting tokens for subsequent parsing.
    void tokenize(std::ifstream& file);

    // Parse a `server` block starting at `it`. Advances `it` as tokens are consumed. Constructs and appends a ServerConfig to `_servers` representing the parsed block.
    void parseServerBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end);

    // Parse a `location` block and attach it to `currentServer`.
    void parseLocationBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end, ServerConfig& currentServer);

public:
    ConfigParser(const std::string& configFilePath);
    ~ConfigParser();

    // This runs tokenization and parsing. On error it throws
    void parse();

    // Validate the parsed configuration for semantic correctness.Checks for conflicting ports, missing required directives, and
    void validate();

    // @return A vector of ServerConfig objects parsed from the file.
    std::vector<ServerConfig> getServers() const;

    // Exception type thrown for parsing and validation errors.
    class ConfigException : public std::exception {
    private:
        std::string _msg;
    public:
        // Construct an exception with a message.
        ConfigException(const std::string& msg);
        virtual ~ConfigException() throw();
        virtual const char* what() const throw();
    };
};

#endif