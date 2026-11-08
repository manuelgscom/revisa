#pragma once
// The long-running and token subcommands, each in a file of its own:
//   serve_cmd.cpp  revisa serve   (revisa_server)
//   lsp_cmd.cpp    revisa lsp     (revisa_lsp)
//   token_cmd.cpp  revisa token   (core/tokens)

#include <memory>
#include <string>

#include "cli/args.hpp"
#include "cli/cli.hpp"
#include "core/check/check.hpp"
#include "core/config/config.hpp"
#include "core/infer/engine.hpp"

namespace revisa::cli {

// The engine of serve and lsp: the injected one, the REVISA_FAKE_* one (test hooks) or the model.
// Never asks to download it (stdin is not the user's there): a missing model is an error.
// On failure engine is null and exit_code is set, with the message already written.
struct ServiceEngine {
    std::unique_ptr<infer::Engine> engine;
    std::string model_name;  // the "model" of the API responses
    std::string model_id;    // tells models apart in the sentence cache (the GGUF path)
    int exit_code = -1;
};
ServiceEngine open_service_engine(const Args& args, const config::Config& cfg, const Host& host);

// check::Options from the config (disabled types) and, with test hooks, the guard thresholds from
// REVISA_GUARD_MAX_CHANGE_RATIO and REVISA_GUARD_MAX_OTRO (guard calibration only).
check::Options check_options(const Args& args, const config::Config& cfg);

int run_serve(const Args& args, const Host& host);
int run_lsp(const Args& args, const Host& host);
int run_token(const Args& args, const Host& host);

}  // namespace revisa::cli
