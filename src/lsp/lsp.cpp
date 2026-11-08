#include "lsp/lsp.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <utility>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/queue/queue.hpp"
#include "lsp/markdown.hpp"
#include "lsp/position.hpp"

namespace revisa::lsp {

using json = nlohmann::json;

// --- Framing -------------------------------------------------------------------------------

namespace {

constexpr std::size_t kMaxBody = std::size_t{64} << 20;  // larger bodies are skipped

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::optional<std::size_t> parse_length(std::string_view v) {
    if (v.empty() || v.size() > 12) return std::nullopt;
    std::size_t n = 0;
    for (const char c : v) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + static_cast<std::size_t>(c - '0');
    }
    return n;
}

}  // namespace

std::optional<std::string> read_message(std::istream& in) {
    for (;;) {
        std::optional<std::size_t> length;
        std::string line;
        bool any = false;
        for (;;) {
            if (!std::getline(in, line)) return std::nullopt;
            const std::string_view l = trim(line);
            if (l.empty()) {
                if (any) break;
                continue;  // stray blank lines between messages
            }
            any = true;
            const std::size_t colon = l.find(':');
            if (colon != std::string_view::npos && iequals(trim(l.substr(0, colon)), "Content-Length")) {
                length = parse_length(trim(l.substr(colon + 1)));
            }
        }
        if (!length) continue;
        if (*length > kMaxBody) {
            in.ignore(static_cast<std::streamsize>(*length));
            if (!in) return std::nullopt;
            continue;
        }
        std::string body(*length, '\0');
        in.read(body.data(), static_cast<std::streamsize>(*length));
        if (static_cast<std::size_t>(in.gcount()) != *length) return std::nullopt;
        return body;
    }
}

void write_message(std::ostream& out, std::string_view body) {
    out << "Content-Length: " << body.size() << "\r\n\r\n";
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.flush();
}

// --- Server --------------------------------------------------------------------------------

namespace {

using Clock = std::chrono::steady_clock;

// JSON-RPC error codes.
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kServerNotInitialized = -32002;

constexpr int kSeverityInformation = 3;
constexpr int kMessageError = 1;

bool supported_language(std::string_view id) {
    // "text" is what Neovim sends for plain-text buffers (its filetype).
    return id == "markdown" || id == "plaintext" || id == "text" || id == "gitcommit" || id == "git-commit";
}

// One kept edit of the last check, over the document's text.
struct Fix {
    std::size_t byte_start = 0, byte_end = 0;
    std::string from, to, type, message;
};

struct Doc {
    std::string language;
    std::string text;
    std::int64_t version = 0;      // the client's
    std::uint64_t gen = 0;         // the server's: changes with every new text
    std::optional<Clock::time_point> due;  // when to check it (debounce); nullopt = submitted
    std::uint64_t checked_gen = 0;  // gen of fixes (0 = none yet)
    std::vector<Fix> fixes;
};

std::string guillemets(std::string_view s) { return "«" + std::string(s) + "»"; }

std::string diagnostic_message(const check::Edit& e) {
    if (e.explanation) return *e.explanation;
    if (e.from.empty()) return "Añadir " + guillemets(e.to);
    if (e.to.empty()) return "Quitar " + guillemets(e.from);
    return "Cambiar " + guillemets(e.from) + " por " + guillemets(e.to);
}

std::string fix_title(const Fix& f) {
    if (f.to.empty()) return "Quitar " + guillemets(f.from);
    if (f.from.empty()) return "Añadir " + guillemets(f.to);
    return "Cambiar a " + guillemets(f.to);
}

// The edits whose range in the original text is exactly their `from`: an edit that touches a
// masked part (Markdown code, URL, HTML) would not be, and is dropped.
std::vector<Fix> make_fixes(std::string_view text, const std::vector<check::Edit>& edits) {
    std::vector<Fix> out;
    for (const check::Edit& e : edits) {
        if (e.byte_end < e.byte_start || e.byte_end > text.size()) continue;
        if (text.substr(e.byte_start, e.byte_end - e.byte_start) != e.from) continue;
        out.push_back({e.byte_start, e.byte_end, e.from, e.to, std::string(catalog::type_id(e.type)),
                       diagnostic_message(e)});
    }
    return out;
}

json position_json(Position p) { return {{"line", p.line}, {"character", p.character}}; }

json range_json(const LineMap& map, std::size_t b, std::size_t e, Encoding enc) {
    return {{"start", position_json(map.position(b, enc))}, {"end", position_json(map.position(e, enc))}};
}

Position position_from(const json& j) {
    const auto field = [&](const char* name) {
        const std::int64_t v = j.at(name).get<std::int64_t>();
        return static_cast<std::size_t>(std::max<std::int64_t>(v, 0));
    };
    return {field("line"), field("character")};
}

json diagnostic_json(const LineMap& map, const Fix& f, Encoding enc) {
    return {{"range", range_json(map, f.byte_start, f.byte_end, enc)},
            {"severity", kSeverityInformation},
            {"source", "revisa"},
            {"code", f.type},
            {"message", f.message},
            {"data", {{"from", f.from}, {"to", f.to}}}};
}

json text_edit_json(const LineMap& map, const Fix& f, Encoding enc) {
    return {{"range", range_json(map, f.byte_start, f.byte_end, enc)}, {"newText", f.to}};
}

bool blank_line(std::string_view text, const LineMap& map, std::size_t line) {
    for (std::size_t i = map.line_start(line); i < map.line_end(line); ++i) {
        if (text[i] != ' ' && text[i] != '\t') return false;
    }
    return true;
}

// The paragraph (lines between blank lines) that contains byte, as a byte range.
std::pair<std::size_t, std::size_t> paragraph(std::string_view text, const LineMap& map, std::size_t byte) {
    std::size_t first = map.line_of(byte);
    std::size_t last = first;
    if (!blank_line(text, map, first)) {
        while (first > 0 && !blank_line(text, map, first - 1)) --first;
        while (last + 1 < map.line_count() && !blank_line(text, map, last + 1)) ++last;
    }
    return {map.line_start(first), map.line_end(last)};
}

std::string engine_error_message(const check::Result& r) {
    std::string m = r.error == "load_failed" ? "revisa no ha podido cargar el modelo"
                                             : "revisa: error del motor (" + r.error + ")";
    if (!r.error_detail.empty()) m += ": " + r.error_detail;
    return m;
}

}  // namespace

struct Server::Impl {
    std::ostream& out;
    infer::Engine& engine;
    const Options options;

    std::mutex out_mutex;  // every write to out

    // Shared with the worker and the inference thread. Lock order: mutex, then out_mutex.
    std::mutex mutex;
    std::condition_variable wake;  // a document became due, or stop
    std::map<std::string, Doc> docs;
    Encoding encoding = Encoding::Utf16;
    std::uint64_t next_gen = 1;
    bool error_shown = false;
    bool stop = false;

    // Reader thread only.
    bool initialized = false;
    bool shutdown = false;

    std::thread worker;
    queue::Queue queue;  // last: destroyed first, while what its jobs use still exists

    Impl(std::ostream& o, infer::Engine& e, const Options& opts) : out(o), engine(e), options(opts) {}

    // --- Output ---

    void send(const json& message) {
        const std::string body = message.dump(-1, ' ', false, json::error_handler_t::replace);
        std::lock_guard lock(out_mutex);
        write_message(out, body);
    }

    void reply(const json& id, json result) {
        send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
    }

    void reply_error(const json& id, int code, const std::string& message) {
        send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
    }

    void notify(const char* method, json params) {
        send({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
    }

    // --- Checking ---

    // With mutex held.
    void publish(const std::string& uri, const Doc& d) {
        const LineMap map(d.text);
        json diagnostics = json::array();
        for (const Fix& f : d.fixes) diagnostics.push_back(diagnostic_json(map, f, encoding));
        notify("textDocument/publishDiagnostics",
               {{"uri", uri}, {"version", d.version}, {"diagnostics", std::move(diagnostics)}});
    }

    // With mutex held. Sends the current text of the document to the queue.
    void submit(const std::string& uri, Doc& d) {
        d.due.reset();
        queue.submit(uri, [this, uri, gen = d.gen, text = d.text, markdown = d.language == "markdown"] {
            const std::string masked = markdown ? mask_markdown(text) : text;
            check::Result r = check::check_text(masked, engine, options.check);
            finish(uri, gen, text, r);
            return r;
        });
    }

    // On the inference thread: keep and publish the result if the text is still the current one.
    void finish(const std::string& uri, std::uint64_t gen, std::string_view text, const check::Result& r) {
        std::lock_guard lock(mutex);
        const auto it = docs.find(uri);
        if (it == docs.end() || it->second.gen != gen) return;
        if (!r.error.empty()) {
            if (!error_shown) {
                error_shown = true;
                notify("window/showMessage", {{"type", kMessageError}, {"message", engine_error_message(r)}});
            }
            return;
        }
        Doc& d = it->second;
        d.fixes = make_fixes(text, r.edits);
        d.checked_gen = gen;
        publish(uri, d);
    }

    void work() {
        std::unique_lock lock(mutex);
        while (!stop) {
            const Clock::time_point now = Clock::now();
            std::optional<Clock::time_point> next;
            for (auto& [uri, d] : docs) {
                if (!d.due) continue;
                if (*d.due <= now) submit(uri, d);
                else if (!next || *d.due < *next) next = d.due;
            }
            if (next) wake.wait_until(lock, *next);
            else wake.wait(lock);
        }
    }

    void flush() {
        {
            std::lock_guard lock(mutex);
            for (auto& [uri, d] : docs) {
                if (d.due) submit(uri, d);
            }
        }
        queue.drain();
    }

    // With mutex held: a new text for the document.
    void changed(Doc& d) {
        d.gen = next_gen++;
        d.due = Clock::now() + options.debounce;
        wake.notify_one();
    }

    // --- Messages ---

    json initialize(const json& params) {
        Encoding chosen = Encoding::Utf16;
        const json* caps = params.contains("capabilities") ? &params["capabilities"] : nullptr;
        if (caps && caps->is_object() && caps->contains("general") && (*caps)["general"].is_object() &&
            (*caps)["general"].contains("positionEncodings") &&
            (*caps)["general"]["positionEncodings"].is_array()) {
            const json& offered = (*caps)["general"]["positionEncodings"];
            bool utf16 = false;
            std::optional<Encoding> other;
            for (const json& name : offered) {
                if (!name.is_string()) continue;
                const auto e = parse_encoding(name.get<std::string>());
                if (e == Encoding::Utf16) utf16 = true;
                else if (e && !other) other = e;
            }
            if (!utf16 && other) chosen = *other;
        }
        {
            std::lock_guard lock(mutex);
            encoding = chosen;
        }
        initialized = true;
        return {{"capabilities",
                 {{"positionEncoding", encoding_name(chosen)},
                  {"textDocumentSync", {{"openClose", true}, {"change", 1}}},
                  {"codeActionProvider", {{"codeActionKinds", {"quickfix"}}}}}},
                {"serverInfo", {{"name", "revisa"}, {"version", REVISA_VERSION}}}};
    }

    void did_open(const json& params) {
        const json& td = params.at("textDocument");
        const std::string uri = td.at("uri").get<std::string>();
        const std::string language = td.at("languageId").get<std::string>();
        std::lock_guard lock(mutex);
        if (!supported_language(language)) {
            docs.erase(uri);
            return;
        }
        Doc& d = docs[uri];
        d = Doc{};
        d.language = language;
        d.text = td.at("text").get<std::string>();
        d.version = td.at("version").get<std::int64_t>();
        changed(d);
    }

    void did_change(const json& params) {
        const json& td = params.at("textDocument");
        const std::string uri = td.at("uri").get<std::string>();
        std::lock_guard lock(mutex);
        const auto it = docs.find(uri);
        if (it == docs.end()) return;
        Doc& d = it->second;
        for (const json& c : params.at("contentChanges")) {
            std::string text = c.at("text").get<std::string>();
            if (c.contains("range")) {  // incremental, although we asked for full documents
                const LineMap map(d.text);
                std::size_t b = map.byte(position_from(c["range"].at("start")), encoding);
                std::size_t e = map.byte(position_from(c["range"].at("end")), encoding);
                if (e < b) std::swap(b, e);
                d.text.replace(b, e - b, text);
            } else {
                d.text = std::move(text);
            }
        }
        if (td.contains("version") && td["version"].is_number_integer()) d.version = td["version"].get<std::int64_t>();
        changed(d);
    }

    void did_close(const json& params) {
        const std::string uri = params.at("textDocument").at("uri").get<std::string>();
        std::lock_guard lock(mutex);
        if (docs.erase(uri) == 0) return;
        notify("textDocument/publishDiagnostics", {{"uri", uri}, {"diagnostics", json::array()}});
    }

    json code_action(const json& params) {
        const std::string uri = params.at("textDocument").at("uri").get<std::string>();
        const json& range = params.at("range");
        json actions = json::array();
        std::lock_guard lock(mutex);
        const auto it = docs.find(uri);
        if (it == docs.end() || it->second.checked_gen != it->second.gen) return actions;
        const Doc& d = it->second;
        const LineMap map(d.text);
        std::size_t b = map.byte(position_from(range.at("start")), encoding);
        std::size_t e = map.byte(position_from(range.at("end")), encoding);
        if (e < b) std::swap(b, e);

        for (const Fix& f : d.fixes) {
            if (f.byte_start > e || f.byte_end < b) continue;
            json edit = {{"changes", {{uri, json::array({text_edit_json(map, f, encoding)})}}}};
            actions.push_back({{"title", fix_title(f)},
                               {"kind", "quickfix"},
                               {"diagnostics", json::array({diagnostic_json(map, f, encoding)})},
                               {"isPreferred", true},
                               {"edit", std::move(edit)}});
        }

        // Every fix of the paragraph at once, when there is more than one.
        const auto [pb, pe] = paragraph(d.text, map, b);
        json edits = json::array();
        json diagnostics = json::array();
        for (const Fix& f : d.fixes) {
            if (f.byte_start < pb || f.byte_end > pe) continue;
            edits.push_back(text_edit_json(map, f, encoding));
            diagnostics.push_back(diagnostic_json(map, f, encoding));
        }
        if (edits.size() >= 2) {
            actions.push_back({{"title", "Aplicar todas en este párrafo"},
                               {"kind", "quickfix"},
                               {"diagnostics", std::move(diagnostics)},
                               {"edit", {{"changes", {{uri, std::move(edits)}}}}}});
        }
        return actions;
    }

    void request(const std::string& method, const json& id, const json& params) {
        if (method == "initialize") {
            if (initialized) return reply_error(id, kInvalidRequest, "already initialized");
            return reply(id, initialize(params));
        }
        if (!initialized) return reply_error(id, kServerNotInitialized, "server not initialized");
        if (shutdown) return reply_error(id, kInvalidRequest, "shutdown requested");
        if (method == "shutdown") {
            flush();
            shutdown = true;
            return reply(id, nullptr);
        }
        if (method == "textDocument/codeAction") return reply(id, code_action(params));
        reply_error(id, kMethodNotFound, "method not found: " + method);
    }

    // false on exit.
    bool notification(const std::string& method, const json& params) {
        if (method == "exit") return false;
        if (!initialized || shutdown) return true;
        if (method == "textDocument/didOpen") did_open(params);
        else if (method == "textDocument/didChange") did_change(params);
        else if (method == "textDocument/didClose") did_close(params);
        return true;  // initialized, $/cancelRequest, $/setTrace…: nothing to do
    }

    bool handle(std::string_view body) {
        const json message = json::parse(body, nullptr, false);
        if (!message.is_object() || !message.contains("method") || !message["method"].is_string()) {
            return true;  // malformed, or a response to us: ignored
        }
        const std::string method = message["method"].get<std::string>();
        static const json kNoParams = json::object();
        const json& params = message.contains("params") ? message["params"] : kNoParams;
        const bool is_request = message.contains("id") &&
                                (message["id"].is_number_integer() || message["id"].is_string());
        try {
            if (is_request) {
                request(method, message["id"], params);
                return true;
            }
            return notification(method, params);
        } catch (const json::exception&) {
            if (is_request) reply_error(message["id"], kInvalidParams, "invalid params");
            return true;
        }
    }
};

Server::Server(std::ostream& out, infer::Engine& engine, const Options& options)
    : impl_(std::make_unique<Impl>(out, engine, options)) {
    impl_->worker = std::thread([this] { impl_->work(); });
}

Server::~Server() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wake.notify_all();
    impl_->worker.join();
}

bool Server::handle(std::string_view body) { return impl_->handle(body); }

void Server::flush() { impl_->flush(); }

int Server::exit_code() const { return impl_->shutdown ? 0 : 1; }

int run(std::istream& in, std::ostream& out, infer::Engine& engine, const Options& options) {
    Server server(out, engine, options);
    while (const std::optional<std::string> body = read_message(in)) {
        if (!server.handle(*body)) break;
    }
    return server.exit_code();
}

}  // namespace revisa::lsp
