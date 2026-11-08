#pragma once
// User configuration, "key = value". The CLI never creates or edits it; the tray app does, from
// its Settings dialog (save_config), keeping comments and order.
//   Linux:   $XDG_CONFIG_HOME/revisa/config, else $HOME/.config/revisa/config
//   Windows: %APPDATA%\revisa\config
// Keys today: model (path to a GGUF; "~/" is expanded), threads (1..kMaxThreads),
// disabled_types (catalog ids separated by commas and/or spaces: "MAYUSCULA, ESPACIADO"),
// port (1..65535, of the local server) and hotkey (the tray app's global shortcut, in Qt's
// QKeySequence PortableText format; the core only rejects control characters, the CLI ignores
// it). The server tokens live in a file of their own (core/tokens).
// Blank lines and lines starting with # are ignored. Unknown or repeated keys and empty values
// are errors.

#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "core/catalog/catalog.hpp"

namespace revisa::config {

inline constexpr int kMaxThreads = 256;
inline constexpr int kDefaultPort = 47312;
inline constexpr const char* kDefaultHotkey = "Ctrl+Alt+Space";

struct Config {
    std::string model;  // empty = default model; "~/" already expanded
    int threads = 0;    // 0 = automatic (physical cores)
    std::set<catalog::Type> disabled_types;  // guard 6 of `revisa check`
    int port = kDefaultPort;                 // `revisa serve`
    std::string hotkey = kDefaultHotkey;     // the tray app (QKeySequence PortableText)

    bool operator==(const Config&) const = default;
};

// No config => error in the format "file:line: reason", in Spanish.
struct ConfigResult {
    std::optional<Config> config;
    std::string error;
};

// filename is only used in error messages; home, to expand "~/" in model.
ConfigResult parse_config(std::string_view text, std::string_view filename, std::string_view home = {});

// Path of the configuration file for this OS. Empty if it cannot be determined.
std::string config_path();

// Missing file => defaults.
ConfigResult load_config();

// Writes cfg to the file at path (the tray app's Settings dialog; the CLI never writes). Existing
// "key = value" lines are updated in place, and only when their value changed; comments, blank
// lines and order are kept. Missing keys are appended only when they differ from the default. A
// default with no written form (model empty, threads 0, disabled_types empty) removes its line.
// Parent directories are created; the write is atomic (temporary file + rename). If the existing
// file does not parse, or cfg has a value the parser would not read back, nothing is written:
// returns false with the reason in error (Spanish), if given.
bool save_config(const std::string& path, const Config& cfg, std::string* error);

}  // namespace revisa::config
