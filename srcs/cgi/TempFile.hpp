#ifndef TEMPFILE_HPP
#define TEMPFILE_HPP

#include <string>

// TempFile provides helpers to create temporary files for CGI input
// bodies or other transient storage. This is a simple stub.
class TempFile {
public:
    TempFile();
    ~TempFile();

    // Create a temporary file containing `data` and return its path.
    // Returns empty string on failure.
    static std::string createFromString(const std::string& data);

    // Remove a temporary file at `path` (best-effort).
    static void remove(const std::string& path);
};

#endif
