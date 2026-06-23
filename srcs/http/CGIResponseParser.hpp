#ifndef CGI_RESPONSE_PARSER_HPP
#define CGI_RESPONSE_PARSER_HPP

#include <string>
#include <map>

// Simple parser for CGI output: separates HTTP-like headers from body.
// Real-world CGI output can be more complex; this parser is a starting point.
class CGIResponseParser {
public:
    CGIResponseParser();
    ~CGIResponseParser();

    // Parse raw CGI stdout into headers map and body string.
    // Returns true on success.
    bool parse(const std::string& raw, std::map<std::string, std::string>& headers, std::string& body) const;
};

#endif
