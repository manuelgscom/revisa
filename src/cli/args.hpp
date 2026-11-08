#pragma once
// Command-line arguments.
//
//   revisa check [FICHERO…] [--json] [--no-guards] [--no-spec] [--model RUTA] [--threads N]
//   revisa check --jsonl [--no-guards] [--no-spec] [--timings] [--model RUTA] [--threads N]
//                                             (cases by stdin)
//   revisa check --jsonl --replay [--no-guards]   (external corrections by stdin, no model)
//   revisa diff [--json] ORIGINAL CORREGIDO
//   revisa diff --jsonl                       (pairs by stdin)
//   revisa classify --jsonl                   (edits by stdin)
//   revisa segment --jsonl                    (texts by stdin: the segments the model would see)
//   revisa serve [--port N] [--no-spec] [--model RUTA] [--threads N]   (local HTTP server)
//   revisa lsp [--no-spec] [--model RUTA] [--threads N]               (language server, stdio)
//   revisa token new NOMBRE | list | revoke NOMBRE                    (server tokens)
//   revisa --print-prompt TEXTO
//   revisa --help | -h | --version
//
// New subcommands (serve, lsp…) are added to Command and to kCommands in args.cpp.

#include <optional>
#include <string>
#include <vector>

namespace revisa::cli {

struct Args {
    enum class Command { None, Check, Diff, Classify, Segment, Serve, Lsp, Token, PrintPrompt, Help, Version };
    enum class Error {
        None,
        NoCommand,
        UnknownCommand,    // bad = the word
        UnknownFlag,       // bad = the flag as written
        MissingValue,      // bad = the flag
        BadThreads,        // bad = the value
        PrintPromptExtra,  // bad = the first extra argument
        FlagNotForCommand, // bad = the flag, command_word = the subcommand
        DiffArgs,          // diff without exactly two texts (or with texts and --jsonl)
        ClassifyArgs,      // classify without --jsonl, or with arguments
        FlagNeeds,         // bad = the flag, needs = the mode it needs (Spanish, for the message)
        CheckJsonlFiles,   // check --jsonl with files
        SegmentArgs,       // segment without --jsonl, or with arguments
        BadPort,           // bad = the value
        NoOperands,        // serve or lsp with operands; bad = the first one
        TokenArgs,         // token without "new NOMBRE", "list" or "revoke NOMBRE"
    };

    Command command = Command::None;
    Error error = Error::None;
    std::string bad;
    std::string command_word;          // the subcommand as written (for FlagNotForCommand)
    std::string needs;                 // FlagNeeds: what the flag needs

    std::vector<std::string> files;    // check: the files (empty = stdin)
    std::vector<std::string> texts;    // diff: ORIGINAL and CORREGIDO; token: the action and NOMBRE
    std::string prompt_text;           // --print-prompt TEXTO
    std::optional<std::string> model;  // --model RUTA (check)
    std::optional<int> threads;        // --threads N (1..config::kMaxThreads) (check, serve, lsp)
    std::optional<int> port;           // --port N (1..65535) (serve)
    bool json = false;                 // --json (check, diff)
    bool jsonl = false;                // --jsonl (check, diff, classify, segment)
    bool replay = false;               // --replay (check --jsonl)
    bool no_guards = false;            // --no-guards (check --json, check --jsonl)
    bool no_spec = false;              // --no-spec (check, serve, lsp): plain greedy decoding
    bool timings = false;              // --timings (check --jsonl): per-segment engine times
};

// args without argv[0]. Flags go anywhere and take "--flag value" or "--flag=value"; "--" ends
// the flags. The first word is the subcommand, the rest are its operands. Precedence: unknown
// flag, missing value, bad value; then --help, --version, --print-prompt; then the subcommand,
// its flags and its operands.
Args parse_args(const std::vector<std::string>& args);

}  // namespace revisa::cli
