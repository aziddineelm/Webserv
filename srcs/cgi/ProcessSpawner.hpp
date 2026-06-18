#ifndef PROCESSSPAWNER_HPP
#define PROCESSSPAWNER_HPP

#include <string>
#include <vector>

// ProcessSpawner provides low-level process utilities used by the CGI
// handler. This is a minimal stub — actual fork/exec handling, non-
// blocking IO and robust cleanup should be implemented here.
class ProcessSpawner {
public:
    ProcessSpawner();
    ~ProcessSpawner();

    // Spawn a process given argv and envp. Returns child PID on success
    // or -1 on error. This stub returns -1.
    int spawn(const std::vector<std::string>& argv,
              const std::vector<std::string>& env,
              int& stdinFd, int& stdoutFd, int& stderrFd);
};

#endif
