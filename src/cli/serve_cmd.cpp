// revisa serve: the local HTTP server in the foreground, until SIGINT or SIGTERM.

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include "cli/commands.hpp"
#include "cli/messages.hpp"
#include "core/infer/cached_engine.hpp"
#include "core/tokens/tokens.hpp"
#include "server/server.hpp"

namespace revisa::cli {

namespace {

// Set by the signal handler; a watcher thread stops the server (stop() is not signal-safe).
std::atomic<bool> g_stop{false};

extern "C" void on_stop_signal(int) { g_stop = true; }

}  // namespace

int run_serve(const Args& args, const Host& host) {
    const config::ConfigResult cfg = config::load_config();
    if (!cfg.config) {
        host.err(msg(Msg::Config, {cfg.error}));
        return kExitError;
    }
    const int port = args.port ? *args.port : cfg.config->port;

    const std::string tokens_path = tokens::tokens_path();
    std::size_t token_count = 0;
    if (!tokens_path.empty()) {
        const tokens::LoadResult loaded = tokens::load(tokens_path);
        if (!loaded.entries) {
            host.err(msg(Msg::TokensFile, {loaded.error}));
            return kExitError;
        }
        token_count = loaded.entries->size();
    }

    ServiceEngine svc = open_service_engine(args, *cfg.config, host);
    if (!svc.engine) return svc.exit_code;
    infer::CachedEngine cached(*svc.engine, svc.model_id);

    server::Options options;
    options.port = port;
    options.model_name = svc.model_name;
    options.check = check_options(args, *cfg.config);
    options.tokens_path = tokens_path;
    server::Server server(cached, std::move(options));
    if (!server.bind()) {
        host.err(msg(Msg::ServeBindFailed, {std::to_string(port)}));
        return kExitError;
    }

    g_stop = false;
    const auto old_int = std::signal(SIGINT, on_stop_signal);
    const auto old_term = std::signal(SIGTERM, on_stop_signal);
    std::atomic<bool> done{false};
    std::thread watcher([&] {
        while (!done) {
            if (g_stop) {
                server.stop();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    host.err(msg(Msg::ServeListening, {std::to_string(server.port())}));
    if (token_count == 0) host.err(msg(Msg::ServeNoTokens));
    server.run();

    done = true;
    watcher.join();
    std::signal(SIGINT, old_int);
    std::signal(SIGTERM, old_term);
    return kExitUnchanged;
}

}  // namespace revisa::cli
