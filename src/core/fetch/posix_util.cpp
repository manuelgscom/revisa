#ifndef _WIN32

#include "core/fetch/posix_util.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>

namespace revisa::fetch::detail {

namespace {

void set_default(int sig) {
    struct sigaction sa {};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

void set_ignore(int sig, struct sigaction* old) {
    struct sigaction sa {};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, old);
}

}  // namespace

pid_t spawn(const std::vector<std::string>& argv) {
    // Everything that allocates memory is prepared before fork.
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    const pid_t pid = fork();
    if (pid != 0) return pid;  // parent (or -1 on failure)

    // Child: only fork-safe functions. SIG_IGN would be inherited across exec.
    set_default(SIGINT);
    set_default(SIGQUIT);
    set_default(SIGPIPE);
    execvp(args[0], args.data());
    _exit(127);
}

namespace {

int exit_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

}  // namespace

int wait_child(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return exit_code(status);
}

bool try_wait_child(pid_t pid, int& code) {
    int status = 0;
    pid_t r = 0;
    while ((r = waitpid(pid, &status, WNOHANG)) < 0) {
        if (errno != EINTR) {
            code = -1;
            return true;
        }
    }
    if (r == 0) return false;
    code = exit_code(status);
    return true;
}

void kill_child(pid_t pid) {
    kill(pid, SIGTERM);
    wait_child(pid);
}

IgnoreInterrupts::IgnoreInterrupts() {
    set_ignore(SIGINT, &old_int_);
    set_ignore(SIGQUIT, &old_quit_);
}

IgnoreInterrupts::~IgnoreInterrupts() {
    sigaction(SIGINT, &old_int_, nullptr);
    sigaction(SIGQUIT, &old_quit_, nullptr);
}

}  // namespace revisa::fetch::detail

#endif  // _WIN32
