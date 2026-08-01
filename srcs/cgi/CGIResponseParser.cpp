#include "CGIResponseParser.hpp"

#include <sstream>

namespace {
    std::string trim(const std::string& s) {
        if (s.empty()) return s;
        size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
            ++start;
        }
        size_t end = s.size();
        while (end > start && (s[end - 1] == '\r' || s[end - 1] == ' ' || s[end - 1] == '\t')) {
            --end;
        }
        return s.substr(start, end - start);
    }

    std::string trimStatusValue(const std::string& s) {
        std::string t = trim(s);
        if (t.compare(0, 5, "HTTP/") == 0) {
            size_t pos = t.find(' ');
            if (pos != std::string::npos) return trim(t.substr(pos + 1));
        }
        return t;
    }
}

CGIResponseParser::CGIResponseParser() {}
CGIResponseParser::~CGIResponseParser() {}

bool CGIResponseParser::parse(const std::string& raw, std::map<std::string, std::string>& headers, std::string& body) const {
    headers.clear();
    body.clear();

    size_t headerEnd = raw.find("\r\n\r\n");
    size_t sepLen = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = raw.find("\n\n");
        sepLen = 2;
    }

    std::string headerBlock;
    if (headerEnd != std::string::npos) {
        headerBlock = raw.substr(0, headerEnd);
        body = raw.substr(headerEnd + sepLen);
    } else {
        headerBlock = raw;
    }

    std::istringstream ss(headerBlock);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = trim(line.substr(pos + 1));
        if (key == "Status" || key == "status" || key == "STATUS") {
            headers["Status"] = trimStatusValue(val);
        } else {
            headers[key] = val;
        }
    }

    return true;
}
