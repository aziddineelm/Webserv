#include "CGIResponseParser.hpp"

#include <sstream>

namespace {
std::string trimRight(const std::string& s) {
    if (s.empty()) return s;
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == '\r' || s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    return s.substr(0, end);
}

std::string trimLeft(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    return s.substr(start);
}

std::string trim(const std::string& s) {
    return trimLeft(trimRight(s));
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
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
        line = trimRight(line);
        if (line.empty()) continue;
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = trimLeft(line.substr(pos + 1));
        if (iequals(key, "Status")) {
            headers["Status"] = trimStatusValue(val);
        } else {
            headers[key] = val;
        }
    }

    return true;
}
