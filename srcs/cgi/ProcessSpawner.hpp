#ifndef PROCESSSPAWNER_HPP
#define PROCESSSPAWNER_HPP

#include <string>
#include <vector>

// ProcessSpawner provides low-level process creation for the CGI handler.
//
// Responsibilities:
//   - Creates three pipes (stdin, stdout, stderr) for parent-child IPC.
//   - Forks a child process and redirects its standard streams via dup2().
//   - Resolves the script path to an absolute path and chdir()s to its
//     directory so relative file access works inside CGI scripts.
//   - Calls execve() with the provided argv and environment.
//   - Returns the parent-side pipe FDs so the caller can do non-blocking IO.
class ProcessSpawner {
public:
    ProcessSpawner();
    ~ProcessSpawner();

    // Spawn a process given argv and envp.
    // On success: returns child PID and sets stdinFd (writable),
    //             stdoutFd (readable), stderrFd (readable).
    // On failure: returns -1 and sets all FDs to -1.
    int spawn(const std::vector<std::string>& argv,
              const std::vector<std::string>& env,
              int& stdinFd, int& stdoutFd, int& stderrFd);
};

#endif
