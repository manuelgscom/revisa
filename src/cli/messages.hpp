#pragma once
// User-facing strings. The interface is Spanish only; every message lives in one table.

#include <initializer_list>
#include <string>
#include <string_view>

namespace revisa::cli {

enum class Msg {
    Help,
    Version,           // {} revisa version, {} contract version
    UsageHint,
    NoCommand,
    UnknownCommand,    // {} command
    UnknownFlag,       // {} flag
    MissingValue,      // {} flag
    BadThreads,        // {} value, {} max
    PrintPromptExtra,  // {} first extra argument
    FlagNotForCommand, // {} flag, {} command
    DiffArgs,
    ClassifyArgs,
    Config,            // {} reason ("file:line: reason")
    ReadFile,          // {} file, {} reason
    ReadStdin,         // {} reason
    FileHeader,        // {} file
    ModelMissing,      // {} path
    NoModelsDir,
    DownloadPrompt,    // {} name, {} MB, {} URL
    DownloadNoTty,     // {} name, {} MB, {} URL
    DownloadDeclined,
    DownloadDone,      // {} path
    DownloadFailed,    // {} reason
    LoadFailed,        // {} detail
    DecodeFailed,      // {} detail
    EngineError,       // {} code, {} detail
    DiffNone,
    EditLine,          // {} start, {} end, {} from, {} to, {} type, {} explanation
    EditLineOther,     // {} start, {} end, {} from, {} to
    JsonlLine,         // {} line number, {} reason
    JsonlInvalid,      // {} parser detail
    JsonlNotObject,
    JsonlField,        // {} field
    FlagNeeds,         // {} flag, {} what it needs
    CheckJsonlFiles,
    SegmentArgs,
    BadPort,           // {} value
    NoOperands,        // {} command, {} first operand
    TokenArgs,
    TokensNoPath,
    TokensFile,        // {} reason ("file:line: reason")
    TokenNone,
    TokenListLine,     // {} name, {} created
    TokenCreated,      // {} name
    TokenRevoked,      // {} name
    TokenBadName,
    TokenDuplicate,    // {} name
    TokenNotFound,     // {} name
    ServeListening,    // {} port
    ServeNoTokens,
    ServeBindFailed,   // {} port
    Count_
};

std::string_view msg(Msg m);

// Replaces each "{}" in pattern, in order, with the matching value (missing ones become "").
std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values);

// msg(m) filled with values.
std::string msg(Msg m, std::initializer_list<std::string_view> values);

}  // namespace revisa::cli
