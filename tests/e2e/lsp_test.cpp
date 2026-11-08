// revisa lsp end to end: the real binary speaks LSP over pipes, inside a sandbox (temporary HOME
// and XDG_*, controlled PATH), with the engine driven by REVISA_FAKE_MAP.

#include "doctest.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "pty_session.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using revisa::e2e::ExitStatus;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kTimeoutMs = 20000;

const char* const kWrong = "😀👍 Ya e echo el informe.";
const char* const kRight = "😀👍 Ya he hecho el informe.";

int ms_left(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

// A temporary HOME with the fake engine's map; removed at the end.
struct LspSandbox {
    std::string dir;

    LspSandbox() {
        std::string tmpl = (fs::temp_directory_path() / "revisa-e2e-lsp-XXXXXX").string();
        REQUIRE(mkdtemp(tmpl.data()) != nullptr);
        dir = tmpl;
        std::ofstream(dir + "/fake-map.json", std::ios::binary) << json{{kWrong, kRight}}.dump();
    }
    ~LspSandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    LspSandbox(const LspSandbox&) = delete;
    LspSandbox& operator=(const LspSandbox&) = delete;

    std::vector<std::string> env() const {
        return {"HOME=" + dir,
                "XDG_CONFIG_HOME=" + dir + "/.config",
                "XDG_CACHE_HOME=" + dir + "/.cache",
                "XDG_DATA_HOME=" + dir + "/.local/share",
                "TMPDIR=" + dir,
                "LANG=es_ES.UTF-8",
                "PATH=/usr/bin:/bin",
                "REVISA_FAKE_MAP=" + dir + "/fake-map.json"};
    }
};

// `revisa lsp` with stdin and stdout as pipes (stderr to a file). Killed if still running at the end.
class LspProcess {
public:
    explicit LspProcess(const LspSandbox& box) : err_path_(box.dir + "/stderr.txt") {
        ::signal(SIGPIPE, SIG_IGN);
        int in[2], out[2];
        REQUIRE(::pipe(in) == 0);
        REQUIRE(::pipe(out) == 0);
        const std::vector<std::string> argv{REVISA_BIN, "lsp"};
        const std::vector<std::string> env = box.env();
        std::vector<char*> args, envp;
        for (const auto& s : argv) args.push_back(const_cast<char*>(s.c_str()));
        args.push_back(nullptr);
        for (const auto& s : env) envp.push_back(const_cast<char*>(s.c_str()));
        envp.push_back(nullptr);
        pid_ = ::fork();
        REQUIRE(pid_ >= 0);
        if (pid_ == 0) {
            ::dup2(in[0], 0);
            ::dup2(out[1], 1);
            const int err = ::open(err_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (err >= 0) ::dup2(err, 2);
            ::close(in[0]);
            ::close(in[1]);
            ::close(out[0]);
            ::close(out[1]);
            ::execve(args[0], args.data(), envp.data());
            ::_exit(127);
        }
        ::close(in[0]);
        ::close(out[1]);
        to_ = in[1];
        from_ = out[0];
    }

    ~LspProcess() {
        close_stdin();
        if (from_ >= 0) ::close(from_);
        if (!reaped_) {
            ::kill(pid_, SIGKILL);
            int status = 0;
            while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    LspProcess(const LspProcess&) = delete;
    LspProcess& operator=(const LspProcess&) = delete;

    void send(const json& j) {
        const std::string body = j.dump();
        const std::string data = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        std::size_t done = 0;
        while (done < data.size()) {
            const ssize_t n = ::write(to_, data.data() + done, data.size() - done);
            if (n < 0 && errno == EINTR) continue;
            REQUIRE(n > 0);
            done += static_cast<std::size_t>(n);
        }
    }

    void request(int id, const std::string& method, const json& params) {
        send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
    }
    void notify(const std::string& method, const json& params) {
        send({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
    }

    // The first message (from now on) that matches; nullopt on timeout or end of output.
    std::optional<json> wait_for(const std::function<bool(const json&)>& match, int timeout_ms = kTimeoutMs) {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            while (const auto m = take()) {
                if (match(*m)) return m;
            }
            if (eof_) return std::nullopt;
            pollfd p{from_, POLLIN, 0};
            const int r = ::poll(&p, 1, ms_left(deadline));
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return std::nullopt;
            char chunk[4096];
            const ssize_t n = ::read(from_, chunk, sizeof chunk);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) eof_ = true;
            else buffer_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    std::optional<json> response(int id) {
        return wait_for([id](const json& m) { return m.contains("id") && m["id"] == id && !m.contains("method"); });
    }
    std::optional<json> notification(const std::string& method) {
        return wait_for([&method](const json& m) { return m.value("method", "") == method; });
    }

    void close_stdin() {
        if (to_ >= 0) ::close(to_);
        to_ = -1;
    }

    ExitStatus wait_exit(int timeout_ms = kTimeoutMs) {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        ExitStatus s;
        int status = 0;
        for (;;) {
            const pid_t r = ::waitpid(pid_, &status, WNOHANG);
            if (r == pid_) break;
            if (Clock::now() >= deadline) {
                s.timed_out = true;
                return s;  // the destructor kills it
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        reaped_ = true;
        if (WIFEXITED(status)) {
            s.exited = true;
            s.code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            s.signaled = true;
            s.signal = WTERMSIG(status);
        }
        return s;
    }

    std::string stderr_text() const {
        std::ifstream f(err_path_, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

private:
    // One complete message from the buffer, if there is one.
    std::optional<json> take() {
        const std::size_t end = buffer_.find("\r\n\r\n");
        if (end == std::string::npos) return std::nullopt;
        const std::string headers = buffer_.substr(0, end);
        const std::size_t at = headers.find("Content-Length: ");
        REQUIRE(at != std::string::npos);
        const std::size_t length = std::stoul(headers.substr(at + 16));
        if (buffer_.size() < end + 4 + length) return std::nullopt;
        json j = json::parse(buffer_.substr(end + 4, length));
        buffer_.erase(0, end + 4 + length);
        return j;
    }

    std::string err_path_;
    pid_t pid_ = -1;
    int to_ = -1, from_ = -1;
    bool eof_ = false;
    bool reaped_ = false;
    std::string buffer_;
};

json pos(int line, int character) { return {{"line", line}, {"character", character}}; }

json range(int line, int start, int end) { return {{"start", pos(line, start)}, {"end", pos(line, end)}}; }

}  // namespace

TEST_CASE("e2e lsp: initialize, diagnostics in UTF-16, code action, shutdown and exit") {
    LspSandbox box;
    LspProcess lsp(box);
    const std::string uri = "file:///tmp/informe.md";

    lsp.request(1, "initialize",
                {{"processId", nullptr},
                 {"capabilities", {{"general", {{"positionEncodings", {"utf-16"}}}}}}});
    const auto init = lsp.response(1);
    REQUIRE_MESSAGE(init.has_value(), lsp.stderr_text());
    CHECK((*init)["result"]["capabilities"]["positionEncoding"] == "utf-16");
    CHECK((*init)["result"]["capabilities"]["textDocumentSync"]["change"] == 1);
    CHECK((*init)["result"]["serverInfo"]["name"] == "revisa");
    lsp.notify("initialized", json::object());

    // Two emojis (two UTF-16 units each) before the errors.
    lsp.notify("textDocument/didOpen",
               {{"textDocument",
                 {{"uri", uri}, {"languageId", "markdown"}, {"version", 1},
                  {"text", std::string(kWrong) + "\n\n`e echo` es código.\n"}}}});
    const auto diags = lsp.notification("textDocument/publishDiagnostics");
    REQUIRE_MESSAGE(diags.has_value(), lsp.stderr_text());
    const json& p = (*diags)["params"];
    CHECK(p["uri"] == uri);
    CHECK(p["version"] == 1);
    REQUIRE(p["diagnostics"].size() == 2);
    CHECK(p["diagnostics"][0]["range"] == range(0, 8, 9));
    CHECK(p["diagnostics"][0]["code"] == "VERBO_HABER_H");
    CHECK(p["diagnostics"][0]["source"] == "revisa");
    CHECK(p["diagnostics"][0]["severity"] == 3);
    CHECK(p["diagnostics"][1]["range"] == range(0, 10, 14));
    CHECK(p["diagnostics"][1]["code"] == "ECHO_HECHO");

    // Cursor inside "echo".
    lsp.request(2, "textDocument/codeAction",
                {{"textDocument", {{"uri", uri}}},
                 {"range", range(0, 11, 11)},
                 {"context", {{"diagnostics", p["diagnostics"]}}}});
    const auto actions = lsp.response(2);
    REQUIRE(actions.has_value());
    const json& a = (*actions)["result"];
    REQUIRE(a.size() == 2);
    CHECK(a[0]["title"] == "Cambiar a «hecho»");
    CHECK(a[0]["kind"] == "quickfix");
    CHECK(a[0]["isPreferred"] == true);
    CHECK(a[0]["edit"]["changes"][uri] == json::array({{{"range", range(0, 10, 14)}, {"newText", "hecho"}}}));
    CHECK(a[1]["title"] == "Aplicar todas en este párrafo");
    CHECK(a[1]["edit"]["changes"][uri].size() == 2);

    lsp.request(3, "shutdown", nullptr);
    const auto down = lsp.response(3);
    REQUIRE(down.has_value());
    CHECK(down->contains("result"));
    CHECK((*down)["result"].is_null());
    lsp.notify("exit", nullptr);
    const ExitStatus s = lsp.wait_exit();
    CHECK(s.exited);
    CHECK(s.code == 0);
    CHECK(lsp.stderr_text().empty());
}

TEST_CASE("e2e lsp: the end of stdin without shutdown exits with 1") {
    LspSandbox box;
    LspProcess lsp(box);
    lsp.request(1, "initialize", {{"processId", nullptr}, {"capabilities", json::object()}});
    REQUIRE(lsp.response(1).has_value());
    lsp.close_stdin();
    const ExitStatus s = lsp.wait_exit();
    CHECK(s.exited);
    CHECK(s.code == 1);
}
