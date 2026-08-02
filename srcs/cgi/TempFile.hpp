#ifndef TEMPFILE_HPP
#define TEMPFILE_HPP

#include <string>

class TempFile {
public:
    TempFile();
    ~TempFile();

    static std::string createFromString(const std::string& data);
    static void remove(const std::string& path);
};

#endif
