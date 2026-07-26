#include "ProcessSpawner.hpp"

#include <unistd.h>
#include <stdlib.h>

ProcessSpawner::ProcessSpawner() {}
ProcessSpawner::~ProcessSpawner() {}

int ProcessSpawner::spawn(const std::vector<std::string>& argv,
                          const std::vector<std::string>& env,
                          int& stdinFd, int& stdoutFd, int& stderrFd) {
    stdinFd = -1;
    stdoutFd = -1;
    stderrFd = -1;

    if (argv.empty()) return -1;

    const size_t MAX_PATH_LEN = 4096;
    int inPipe[2] = {-1, -1}, outPipe[2] = {-1, -1}, errPipe[2] = {-1, -1};

    if (pipe(inPipe) == -1 || pipe(outPipe) == -1 || pipe(errPipe) == -1) {
        if (inPipe[0] != -1) { close(inPipe[0]); close(inPipe[1]); }
        if (outPipe[0] != -1) { close(outPipe[0]); close(outPipe[1]); }
        if (errPipe[0] != -1) { close(errPipe[0]); close(errPipe[1]); }
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(inPipe[0]); close(inPipe[1]);
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        return -1;
    }

    if (pid == 0) {
        // Child: wire up stdin/stdout/stderr and exec.
        if (dup2(inPipe[0], STDIN_FILENO) == -1
            || dup2(outPipe[1], STDOUT_FILENO) == -1
            || dup2(errPipe[1], STDERR_FILENO) == -1) {
            _exit(1);
        }

        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);

        // Resolve script to absolute path before chdir so execve can find it.
        char resolvedPath[MAX_PATH_LEN];
        std::string absScript = argv[0];
        if (realpath(argv[0].c_str(), resolvedPath) != NULL) {
            absScript = resolvedPath;
        }

        std::vector<char*> argvC;
        argvC.reserve(argv.size() + 1);
        argvC.push_back(const_cast<char*>(absScript.c_str()));
        for (size_t i = 1; i < argv.size(); ++i) {
            argvC.push_back(const_cast<char*>(argv[i].c_str()));
        }
        argvC.push_back(NULL);

        std::vector<char*> envC;
        envC.reserve(env.size() + 1);
        for (size_t i = 0; i < env.size(); ++i) {
            envC.push_back(const_cast<char*>(env[i].c_str()));
        }
        envC.push_back(NULL);

        // Change to the script's directory for relative path access.
        size_t lastSlash = absScript.rfind('/');
        if (lastSlash != std::string::npos) {
            std::string scriptDir = absScript.substr(0, lastSlash);
            if (!scriptDir.empty()) {
                if (chdir(scriptDir.c_str()) != 0) {
                    // Non-fatal: script may still work without chdir.
                    // stderr is already redirected to the parent's pipe.
                    write(STDERR_FILENO, "CGI: chdir failed\n", 18);
                }
            }
        }

        execve(argvC[0], &argvC[0], &envC[0]);
        _exit(127);
    }

    // Parent: close child ends and return parent ends.
    close(inPipe[0]);
    close(outPipe[1]);
    close(errPipe[1]);

    stdinFd = inPipe[1];
    stdoutFd = outPipe[0];
    stderrFd = errPipe[0];

    return pid;
}
