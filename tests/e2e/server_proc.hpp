#pragma once
// A long-running child process for the server e2e: started with its own environment, output to a
// file, and always stopped (SIGTERM, then SIGKILL) and reaped when the object goes away.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace revisa::e2e {

// A port that was free a moment ago on 127.0.0.1 (the kernel's choice).
inline int free_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof addr;
    int port = -1;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0)
        port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

class BackgroundProcess {
public:
    // argv[0] is the program (full path). env: "KEY=value"; nothing is inherited.
    // stdin is /dev/null; stdout and stderr go to output_path.
    BackgroundProcess(const std::vector<std::string>& argv, const std::vector<std::string>& env,
                      const std::string& output_path) {
        std::vector<char*> a, e;
        for (const auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        for (const auto& s : env) e.push_back(const_cast<char*>(s.c_str()));
        e.push_back(nullptr);
        pid_ = ::fork();
        if (pid_ == 0) {
            const int in = ::open("/dev/null", O_RDONLY);
            const int out = ::open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (in < 0 || out < 0) ::_exit(127);
            ::dup2(in, 0);
            ::dup2(out, 1);
            ::dup2(out, 2);
            ::execve(a[0], a.data(), e.data());
            ::_exit(127);
        }
    }
    ~BackgroundProcess() {
        if (!stop(SIGTERM, 3000)) stop(SIGKILL, 3000);
    }
    BackgroundProcess(const BackgroundProcess&) = delete;
    BackgroundProcess& operator=(const BackgroundProcess&) = delete;

    bool started() const { return pid_ > 0; }

    // Sends sig and waits up to timeout_ms for the exit. true once reaped.
    bool stop(int sig, int timeout_ms) {
        if (pid_ <= 0 || status_) return true;
        ::kill(pid_, sig);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            int st = 0;
            if (::waitpid(pid_, &st, WNOHANG) == pid_) {
                status_ = st;
                return true;
            }
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // The exit code after a stop, or nullopt if it did not exit normally.
    std::optional<int> exit_code() const {
        if (!status_ || !WIFEXITED(*status_)) return std::nullopt;
        return WEXITSTATUS(*status_);
    }

    // true while the child has not exited (it is reaped if it has).
    bool alive() {
        if (pid_ <= 0 || status_) return false;
        int st = 0;
        if (::waitpid(pid_, &st, WNOHANG) == pid_) {
            status_ = st;
            return false;
        }
        return true;
    }

private:
    pid_t pid_ = -1;
    std::optional<int> status_;
};

}  // namespace revisa::e2e
