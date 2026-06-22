#include "TempFile.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

TempFile::TempFile() {}
TempFile::~TempFile() {}

std::string TempFile::createFromString(const std::string& data) {
    // Create a temporary file in /tmp with unique name.
    char tmpl[] = "/tmp/webserv_tmp_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd == -1) return std::string();
    fchmod(fd, S_IRUSR | S_IWUSR);
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = write(fd, data.data() + written, data.size() - written);
        if (n <= 0) {
            close(fd);
            unlink(tmpl);
            return std::string();
        }
        written += static_cast<size_t>(n);
    }
    close(fd);
    return std::string(tmpl);
}

void TempFile::remove(const std::string& path) {
    if (!path.empty()) unlink(path.c_str());
}
