#ifndef PROCESSSPAWNER_HPP
#define PROCESSSPAWNER_HPP

#include <string>
#include <vector>

class ProcessSpawner {
public:
    ProcessSpawner();
    ~ProcessSpawner();

    // On success: returns child PID
    // On failure: returns -1 and sets all FDs to -1.
    int spawn(const std::vector<std::string>& argv,
              const std::vector<std::string>& env,
              int& stdinFd, int& stdoutFd, int& stderrFd);
};

#endif
