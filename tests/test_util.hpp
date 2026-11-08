#pragma once
// Portable helpers for the unit tests: environment variables and temporary directories.
// On Windows an empty value deletes the variable (_putenv_s), so "set but empty" cannot be
// represented there; tests that need it are POSIX-only.

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <random>
#include <stdexcept>
#else
#include <vector>
#endif

#include "doctest.h"

namespace revisa::test {

#ifdef _WIN32
inline constexpr char kSep = '\\';
// The variable "~/" expands to.
inline constexpr const char* kHomeVar = "USERPROFILE";
#else
inline constexpr char kSep = '/';
inline constexpr const char* kHomeVar = "HOME";
#endif

// a + separator + b, with the separator the code under test uses on this OS.
inline std::string join(const std::string& a, const std::string& b) { return a + kSep + b; }

inline void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

inline void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// Creates a new, empty directory named prefix + a unique suffix in the system temp directory
// (POSIX: $TMPDIR or /tmp) and returns its path.
inline std::string make_temp_dir(const std::string& prefix) {
#ifdef _WIN32
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    std::random_device rd;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::filesystem::path p = base / (prefix + std::to_string(rd()));
        std::error_code ec;
        if (std::filesystem::create_directory(p, ec)) return p.string();
    }
    throw std::runtime_error("cannot create a temporary directory in " + base.string());
#else
    const char* t = std::getenv("TMPDIR");
    const std::string tmpl = std::string(t != nullptr && *t != '\0' ? t : "/tmp") + "/" + prefix + "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    REQUIRE(mkdtemp(buf.data()) != nullptr);
    return buf.data();
#endif
}

inline void remove_tree(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

// Own temporary directory, removed on destruction.
struct TempDir {
    std::string path;
    explicit TempDir(const std::string& prefix = "revisa_test_") : path(make_temp_dir(prefix)) {}
    ~TempDir() { remove_tree(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// Saves an environment variable and restores it when going out of scope.
class EnvGuard {
public:
    explicit EnvGuard(const char* name) : name_(name) {
        if (const char* v = std::getenv(name)) old_ = v;
    }
    ~EnvGuard() {
        if (old_) set_env(name_, *old_);
        else unset_env(name_);
    }
    void set(const std::string& v) { set_env(name_, v); }
    void unset() { unset_env(name_); }
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    const char* name_;
    std::optional<std::string> old_;
};

}  // namespace revisa::test
