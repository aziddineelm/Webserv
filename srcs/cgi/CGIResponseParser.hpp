#ifndef CGI_RESPONSE_PARSER_HPP
#define CGI_RESPONSE_PARSER_HPP

#include <string>
#include <map>

class CGIResponseParser {
public:
    CGIResponseParser();
    ~CGIResponseParser();
    bool parse(const std::string& raw, std::map<std::string, std::string>& headers, std::string& body) const;
};

#endif
