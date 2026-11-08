#pragma once
// Entry point of the revisa binary.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/infer/engine.hpp"

namespace revisa::cli {

// The process' terminal, injectable so that run() can be tested in-process.
struct Host {
    std::function<void(std::string_view)> out;
    std::function<void(std::string_view)> err;
    // One line of stdin without "\n" (the download question, the cases of check --jsonl);
    // nullopt = EOF.
    std::function<std::optional<std::string>()> read_line;
    // The whole of stdin (the text to check); nullopt = read error, with a Spanish reason in error.
    std::function<std::optional<std::string>(std::string& error)> read_stdin;
    // Tests only: replaces the model resolution and the engine when set (and not returning nullptr).
    std::function<std::unique_ptr<infer::Engine>()> make_engine;
    bool stdin_tty = false;
    bool stderr_tty = false;
};

// Exit codes.
inline constexpr int kExitUnchanged = 0;
inline constexpr int kExitChanged = 1;
inline constexpr int kExitError = 2;
inline constexpr int kExitInterrupted = 130;

// The whole program over a host (args without argv[0]). Returns the exit code. Reads the config
// file and the input files from disk, and may download the model.
int run(const std::vector<std::string>& args, const Host& host);

// The real process: stdin/stdout/stderr and the SIGINT handler (exit 130).
int run(int argc, char** argv);

}  // namespace revisa::cli
