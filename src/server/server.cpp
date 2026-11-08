#include "server/server.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "core/catalog/catalog.hpp"
#include "core/check/json.hpp"
#include "core/fetch/sha256.hpp"
#include "core/queue/queue.hpp"
#include "core/text/text.hpp"
#include "core/tokens/tokens.hpp"

namespace revisa::server {

namespace {

constexpr const char* kJson = "application/json";
// The warm-up check: a short text that reaches the model, so that it gets loaded.
constexpr std::string_view kWarmUpText = "Hola.";

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

void send_json(httplib::Response& res, int status, const nlohmann::ordered_json& body) {
    res.status = status;
    res.set_content(check::dump_line(body), kJson);
}

void send_error(httplib::Response& res, int status, std::string_view code) {
    nlohmann::ordered_json body;
    body["error"] = code;
    send_json(res, status, body);
}

// The listening socket may be reused right after a restart, but never shared with another live
// socket (cpp-httplib's default is SO_REUSEPORT, which would let a second server share the port).
void socket_options(socket_t sock) {
    int on = 1;
#ifdef _WIN32
    ::setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on);
#else
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
}

}  // namespace

bool host_allowed(std::string_view host, int port) {
    const std::string p = ":" + std::to_string(port);
    return host == "127.0.0.1" + p || host == "localhost" + p;
}

bool origin_allowed(std::optional<std::string_view> origin) {
    if (!origin) return true;
    return starts_with(*origin, "chrome-extension://") || starts_with(*origin, "moz-extension://");
}

struct Server::Impl {
    infer::Engine& engine;
    Options options;
    httplib::Server http;
    std::atomic<bool> ready{false};
    int port = 0;
    std::mutex tokens_mutex;  // pairing writes the tokens file
    queue::Queue queue;       // last: destroyed (joined) first, while the rest still exists

    Impl(infer::Engine& e, Options o) : engine(e), options(std::move(o)), port(options.port) {}

    // The check of one text on the inference thread.
    queue::Queue::Future submit(std::string key, std::string text, check::Options opts) {
        return queue.submit(std::move(key), [this, text = std::move(text), opts = std::move(opts)] {
            return check::check_text(text, engine, opts);
        });
    }

    bool guard(const httplib::Request& req, httplib::Response& res) {
        if (req.get_header_value_count("Host") != 1 || !host_allowed(req.get_header_value("Host"), port)) {
            send_error(res, 403, "forbidden_host");
            return false;
        }
        const std::size_t origins = req.get_header_value_count("Origin");
        std::optional<std::string> origin;
        if (origins > 0) origin = req.get_header_value("Origin");
        if (origins > 1 || !origin_allowed(origin ? std::optional<std::string_view>(*origin) : std::nullopt)) {
            send_error(res, 403, "forbidden_origin");
            return false;
        }
        return true;
    }

    void health(httplib::Response& res) {
        nlohmann::ordered_json body;
        body["version"] = REVISA_VERSION;
        body["model"] = options.model_name;
        body["ready"] = ready.load();
        send_json(res, 200, body);
    }

    void pair(const httplib::Request& req, httplib::Response& res) {
        if (!options.approve_pair || options.tokens_path.empty()) return send_error(res, 403, "pairing_disabled");
        const nlohmann::json j = nlohmann::json::parse(req.body, nullptr, false);
        if (j.is_discarded()) return send_error(res, 400, "invalid_json");
        if (!j.is_object() || !j.contains("name") || !j["name"].is_string())
            return send_error(res, 400, "invalid_request");
        // One pairing at a time: the user answers one dialog, and the file has one writer.
        std::lock_guard lock(tokens_mutex);
        tokens::LoadResult loaded = tokens::load(options.tokens_path);
        if (!loaded.entries) return send_error(res, 500, "tokens_file");
        const std::string name = tokens::unique_name(*loaded.entries, j["name"].get<std::string>());
        if (!options.approve_pair(name)) return send_error(res, 403, "pairing_rejected");
        const tokens::Change c = tokens::add(options.tokens_path, name);
        if (c.status != tokens::Status::Ok) return send_error(res, 500, "tokens_file");
        nlohmann::ordered_json body;
        body["token"] = c.token;
        send_json(res, 200, body);
    }

    void check(const httplib::Request& req, httplib::Response& res) {
        // Authorization: the file is re-read every time (it is tiny), so a revocation is immediate.
        const std::string auth = req.get_header_value("Authorization");
        const std::string_view kBearer = "Bearer ";
        if (!starts_with(auth, kBearer) || auth.size() == kBearer.size())
            return send_error(res, 401, "unauthorized");
        const std::string token = auth.substr(kBearer.size());
        if (options.tokens_path.empty()) return send_error(res, 401, "unauthorized");
        const tokens::LoadResult loaded = tokens::load(options.tokens_path);
        if (!loaded.entries) return send_error(res, 500, "tokens_file");
        if (!tokens::verify(*loaded.entries, token)) return send_error(res, 401, "unauthorized");

        const nlohmann::json j = nlohmann::json::parse(req.body, nullptr, false);
        if (j.is_discarded()) return send_error(res, 400, "invalid_json");
        if (!j.is_object() || !j.contains("text") || !j["text"].is_string())
            return send_error(res, 400, "invalid_request");
        check::Options opts = options.check;
        if (j.contains("disabled_types")) {
            const nlohmann::json& types = j["disabled_types"];
            if (!types.is_array()) return send_error(res, 400, "invalid_request");
            for (const nlohmann::json& t : types) {
                if (!t.is_string()) return send_error(res, 400, "invalid_request");
                const auto type = catalog::type_from_id(t.get<std::string>());
                if (!type) return send_error(res, 400, "unknown_type");
                opts.disabled.insert(*type);
            }
        }
        std::string key;
        if (j.contains("field")) {
            if (!j["field"].is_string()) return send_error(res, 400, "invalid_request");
            key = tokens::hash_token(token) + "\n" + j["field"].get<std::string>();
        }
        std::string text = j["text"].get<std::string>();
        if (text::code_points(text) > kMaxTextChars) return send_error(res, 413, "too_long");

        const std::optional<check::Result> result = submit(std::move(key), std::move(text), std::move(opts)).get();
        if (!result) return send_error(res, 409, "superseded");
        if (!result->error.empty()) return send_error(res, 500, result->error);
        send_json(res, 200, check::response_json(options.model_name, *result));
    }
};

Server::Server(infer::Engine& engine, Options options) : impl_(std::make_unique<Impl>(engine, std::move(options))) {
    Impl& s = *impl_;
    s.http.set_socket_options(socket_options);
    s.http.set_payload_max_length(kMaxBodyBytes);
    s.http.set_pre_routing_handler([&s](const httplib::Request& req, httplib::Response& res) {
        return s.guard(req, res) ? httplib::Server::HandlerResponse::Unhandled
                                 : httplib::Server::HandlerResponse::Handled;
    });
    s.http.Get("/v1/health", [&s](const httplib::Request&, httplib::Response& res) { s.health(res); });
    s.http.Post("/v1/pair", [&s](const httplib::Request& req, httplib::Response& res) { s.pair(req, res); });
    s.http.Post("/v1/check", [&s](const httplib::Request& req, httplib::Response& res) { s.check(req, res); });

    // Warm-up: loads the model on the inference thread; the future is not waited for.
    check::Options warm = s.options.check;
    s.queue.submit("", [&s, warm] {
        check::Result r = check::check_text(kWarmUpText, s.engine, warm);
        s.ready = r.error.empty();
        return r;
    });
}

Server::~Server() { impl_->http.stop(); }

bool Server::bind() {
    Impl& s = *impl_;
    if (s.options.port == 0) {
        const int p = s.http.bind_to_any_port("127.0.0.1");
        if (p <= 0) return false;
        s.port = p;
        return true;
    }
    return s.http.bind_to_port("127.0.0.1", s.options.port);
}

int Server::port() const { return impl_->port; }

void Server::run() { impl_->http.listen_after_bind(); }

void Server::stop() { impl_->http.stop(); }

bool Server::ready() const { return impl_->ready.load(); }

}  // namespace revisa::server
