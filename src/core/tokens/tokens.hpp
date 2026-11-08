#pragma once
// Client tokens of the local server. Only their SHA-256 is stored, one per line, in
// a file of its own next to the config (the config stays read-only for revisa):
//   Linux:   $XDG_CONFIG_HOME/revisa/tokens, else $HOME/.config/revisa/tokens
//   Windows: %APPDATA%\revisa\tokens
// Line format: "<sha256 hex> <name> <created, ISO 8601 UTC>"; blank lines and lines starting with
// # are ignored. Written atomically (temporary file + rename), mode 0600, directory 0700.
// A token is 32 random bytes as 64 hex characters; it is shown once and never stored.
// Error strings are Spanish: they reach the user as they are.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace revisa::tokens {

inline constexpr std::size_t kMaxNameLength = 64;

struct Entry {
    std::string hash;     // SHA-256 hex of the token
    std::string name;     // [A-Za-z0-9._-]{1,64}, unique
    std::string created;  // ISO 8601 UTC: "2025-03-14T10:00:00Z"
};

// Path of the tokens file for this OS. Empty if it cannot be determined.
std::string tokens_path();

// [A-Za-z0-9._-]{1,kMaxNameLength}.
bool valid_name(std::string_view name);

// No entries => error in the format "file:line: reason".
struct LoadResult {
    std::optional<std::vector<Entry>> entries;
    std::string error;
};

// filename is only used in error messages.
LoadResult parse(std::string_view text, std::string_view filename);
std::string serialize(const std::vector<Entry>& entries);

// Missing file => no entries.
LoadResult load(const std::string& path);

// Atomic write of the whole file (creates the directory if missing). false with a reason in error.
bool save(const std::string& path, const std::vector<Entry>& entries, std::string& error);

// Lowercase SHA-256 hex of the token.
std::string hash_token(std::string_view token);

// 32 random bytes (std::random_device) as 64 lowercase hex characters.
std::string new_token();

// The current UTC time, "YYYY-MM-DDTHH:MM:SSZ".
std::string now_utc();

enum class Status { Ok, InvalidName, Duplicate, NotFound, FileError };

struct Change {
    Status status = Status::Ok;
    std::string token;  // add: the new token (shown once)
    std::string error;  // FileError: the reason
};

// Creates a token called name and saves its hash.
Change add(const std::string& path, std::string_view name);

// Removes the token called name.
Change revoke(const std::string& path, std::string_view name);

// true if token matches some entry. Compares the hashes in constant time, over every entry.
bool verify(const std::vector<Entry>& entries, std::string_view token);

// A valid name, not used in entries, from what a client asked for (POST /v1/pair): characters
// outside [A-Za-z0-9._-] become '-', cut to fit, "cliente" if nothing is left, and "-2", "-3"…
// appended while taken.
std::string unique_name(const std::vector<Entry>& entries, std::string_view wanted);

}  // namespace revisa::tokens
