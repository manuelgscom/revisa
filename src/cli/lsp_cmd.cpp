#include <cstdio>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "cli/commands.hpp"
#include "cli/messages.hpp"
#include "core/infer/cached_engine.hpp"
#include "lsp/lsp.hpp"

namespace revisa::cli {

// revisa lsp: the language server over stdin/stdout. stdout carries the protocol only.
int run_lsp(const Args& args, const Host& host) {
    const config::ConfigResult cfg = config::load_config();
    if (!cfg.config) {
        host.err(msg(Msg::Config, {cfg.error}));
        return kExitError;
    }
    ServiceEngine service = open_service_engine(args, *cfg.config, host);
    if (!service.engine) return service.exit_code;
    infer::CachedEngine engine(*service.engine, service.model_id);

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    // cin is tied to cout: each read would flush cout outside the server's write mutex.
    std::cin.tie(nullptr);
    lsp::Options options;
    options.check = check_options(args, *cfg.config);
    return lsp::run(std::cin, std::cout, engine, options);
}

}  // namespace revisa::cli
