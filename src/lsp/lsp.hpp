#pragma once
// revisa_lsp: the language server of `revisa lsp`. JSON-RPC 2.0 over a stream with
// Content-Length framing. Full document sync; after `debounce` without changes a document goes to
// the inference queue (key = its URI, so a newer text cancels a pending older one) and its
// diagnostics are published only if that version is still the current one. Code actions come
// from the server's own last results. Nothing is logged: the output carries protocol only.

#include <chrono>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "core/check/check.hpp"
#include "core/infer/engine.hpp"

namespace revisa::lsp {

struct Options {
    check::Options check;
    std::chrono::milliseconds debounce{300};
};

// --- Framing -------------------------------------------------------------------------------

// The body of the next message: headers ("Content-Length" required, others such as
// "Content-Type" ignored), a blank line, then Content-Length bytes. A header block without
// Content-Length is skipped. nullopt at the end of the stream.
std::optional<std::string> read_message(std::istream& in);

// Writes one framed message and flushes.
void write_message(std::ostream& out, std::string_view body);

// --- Server --------------------------------------------------------------------------------

// One session. handle() is called from one thread (the reader); the results are written from
// the inference thread. out is shared under one mutex. The engine is only used from the
// inference thread. Destroying the server cancels pending work and waits for the running job.
class Server {
public:
    Server(std::ostream& out, infer::Engine& engine, const Options& options);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // One message body. false after `exit`: the session is over.
    bool handle(std::string_view body);

    // Submits every debounced document now and waits until the queue is empty (as `shutdown`).
    void flush();

    // 0 if `shutdown` came before `exit` (or the end of the input), 1 otherwise.
    int exit_code() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The whole session over in and out. Returns the process exit code.
int run(std::istream& in, std::ostream& out, infer::Engine& engine, const Options& options);

}  // namespace revisa::lsp
