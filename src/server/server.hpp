#pragma once
// revisa_server: the local HTTP server of `revisa serve`.
//   GET  /v1/health  no token  {"version", "model", "ready"}
//   POST /v1/pair    no token  {"name"} -> {"token"} if approve_pair says yes; 403 without it
//   POST /v1/check   Bearer    the request and response of the JSON contract
// Listens on 127.0.0.1 only. Every request first passes the Host check (DNS rebinding) and the
// Origin check (no web page may call; browser extensions and clients without Origin may).
// The engine is used only from the inference thread (core/queue). User text is never logged.

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/check/check.hpp"
#include "core/infer/engine.hpp"

namespace revisa::server {

// Code points per text in POST /v1/check; more is 413.
inline constexpr std::size_t kMaxTextChars = 20000;
// Bytes per request body (UTF-8 is at most 4 bytes per code point, plus the JSON around it).
inline constexpr std::size_t kMaxBodyBytes = kMaxTextChars * 4 + 4096;

struct Options {
    int port = 0;            // 0 = any free port (tests); see Server::port()
    std::string model_name;  // the "model" of the responses
    check::Options check;    // disabled types of the config, guards
    std::string tokens_path; // re-read on every check, so that a revocation is immediate
    // POST /v1/pair: asks the user (a hosting GUI app; none in v1). Called on an HTTP thread
    // with the sanitized client name. Unset = pairing disabled (403), as in `revisa serve`.
    std::function<bool(const std::string& client_name)> approve_pair;
};

// Host header: exactly "127.0.0.1:<port>" or "localhost:<port>".
bool host_allowed(std::string_view host, int port);

// Origin header: absent, chrome-extension://… or moz-extension://…; anything else is rejected.
bool origin_allowed(std::optional<std::string_view> origin);

class Server {
public:
    // engine must outlive the server; it is only called from the server's inference thread.
    // A warm-up check starts at once (it loads the model); /v1/health says ready when it ends.
    Server(infer::Engine& engine, Options options);
    ~Server();  // stops and waits for the running check
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Binds 127.0.0.1:options.port. false if the port cannot be used.
    bool bind();
    // The bound port (after bind()).
    int port() const;
    // Serves until stop(). Needs bind().
    void run();
    // From any thread (not from a signal handler). run() returns soon after.
    void stop();
    // true once the warm-up check has finished without an engine error.
    bool ready() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace revisa::server
