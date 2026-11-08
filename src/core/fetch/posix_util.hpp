#pragma once
// Internal POSIX helpers for the fetch module: spawn a child, wait for or kill it and ignore Ctrl-C.

#ifndef _WIN32

#include <signal.h>
#include <sys/types.h>

#include <string>
#include <vector>

namespace revisa::fetch::detail {

// fork + execvp(argv[0], argv). In the child, resets SIGINT, SIGQUIT and SIGPIPE to SIG_DFL;
// if exec fails the child exits with 127. Returns the child's pid, or -1 if fork fails.
pid_t spawn(const std::vector<std::string>& argv);

// waitpid retrying on EINTR. Returns the exit code, 128+N if killed by
// signal N, or -1 if waitpid fails.
int wait_child(pid_t pid);

// Non-blocking wait_child: true (and code set as in wait_child) once the child has ended.
bool try_wait_child(pid_t pid, int& code);

// SIGTERM to the child and wait for it.
void kill_child(pid_t pid);

// While alive, the process ignores SIGINT and SIGQUIT (like system()). Restores on destruction.
class IgnoreInterrupts {
public:
    IgnoreInterrupts();
    ~IgnoreInterrupts();
    IgnoreInterrupts(const IgnoreInterrupts&) = delete;
    IgnoreInterrupts& operator=(const IgnoreInterrupts&) = delete;

private:
    struct sigaction old_int_{};
    struct sigaction old_quit_{};
};

}  // namespace revisa::fetch::detail

#endif  // _WIN32
