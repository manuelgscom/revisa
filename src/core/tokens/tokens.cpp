#include "core/tokens/tokens.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <random>

#ifndef _WIN32
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "core/config/config.hpp"
#include "core/fetch/sha256.hpp"

namespace revisa::tokens {

namespace {

namespace fs = std::filesystem;

bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
}

bool is_hex64(std::string_view s) {
    if (s.size() != 64) return false;
    for (const char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// "YYYY-MM-DDTHH:MM:SSZ" (digits where the pattern has them).
bool is_iso_utc(std::string_view s) {
    static constexpr std::string_view kPattern = "dddd-dd-ddTdd:dd:ddZ";
    if (s.size() != kPattern.size()) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (kPattern[i] == 'd') {
            if (s[i] < '0' || s[i] > '9') return false;
        } else if (s[i] != kPattern[i]) {
            return false;
        }
    }
    return true;
}

// Fields separated by runs of spaces or tabs.
std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

LoadResult fail(std::string_view filename, std::size_t lineno, const std::string& why) {
    return {std::nullopt, std::string(filename) + ":" + std::to_string(lineno) + ": " + why};
}

bool read_file(const std::string& path, std::string& text, bool& missing, std::string& error) {
    missing = false;
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        error = path + ": no se puede leer: es un directorio";
        return false;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        if (errno == ENOENT) {
            missing = true;
            return true;
        }
        error = path + ": no se puede leer: " + std::strerror(errno);
        return false;
    }
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) {
        error = path + ": no se puede leer";
        return false;
    }
    return true;
}

bool write_all(std::FILE* f, const std::string& data) {
    return std::fwrite(data.data(), 1, data.size(), f) == data.size() && std::fflush(f) == 0;
}

}  // namespace

std::string tokens_path() {
    // Next to the config: the same directory, file "tokens".
    const std::string config = config::config_path();
    if (config.empty()) return {};
    return (fs::path(config).parent_path() / "tokens").string();
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength) return false;
    for (const char c : name)
        if (!is_name_char(c)) return false;
    return true;
}

LoadResult parse(std::string_view text, std::string_view filename) {
    std::vector<Entry> entries;
    std::size_t lineno = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::vector<std::string_view> f = split_fields(line);
        if (f.empty() || f[0].front() == '#') continue;
        if (f.size() != 3) return fail(filename, lineno, "se esperaban tres campos: SHA-256, nombre y fecha");
        if (!is_hex64(f[0])) return fail(filename, lineno, "el SHA-256 no es válido");
        if (!valid_name(f[1])) return fail(filename, lineno, "el nombre no es válido");
        if (!is_iso_utc(f[2])) return fail(filename, lineno, "la fecha no es válida");
        for (const Entry& e : entries)
            if (e.name == f[1]) return fail(filename, lineno, "nombre repetido: " + std::string(f[1]));
        entries.push_back({std::string(f[0]), std::string(f[1]), std::string(f[2])});
    }
    return {std::move(entries), {}};
}

std::string serialize(const std::vector<Entry>& entries) {
    std::string out = "# revisa: tokens del servidor local (solo su SHA-256). Se gestionan con «revisa token».\n";
    for (const Entry& e : entries) out += e.hash + " " + e.name + " " + e.created + "\n";
    return out;
}

LoadResult load(const std::string& path) {
    std::string text, error;
    bool missing = false;
    if (!read_file(path, text, missing, error)) return {std::nullopt, error};
    if (missing) return {std::vector<Entry>{}, {}};
    return parse(text, path);
}

bool save(const std::string& path, const std::vector<Entry>& entries, std::string& error) {
    const fs::path dir = fs::path(path).parent_path();
    std::error_code ec;
    if (!dir.empty() && !fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        if (ec) {
            error = dir.string() + ": no se puede crear: " + ec.message();
            return false;
        }
        fs::permissions(dir, fs::perms::owner_all, ec);  // 0700
    }
    const std::string data = serialize(entries);
#ifdef _WIN32
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) {
        error = tmp + ": no se puede escribir: " + std::strerror(errno);
        return false;
    }
    const bool ok = write_all(f, data);
    if (std::fclose(f) != 0 || !ok) {
        error = tmp + ": no se puede escribir";
        fs::remove(tmp, ec);
        return false;
    }
#else
    std::string tmp = path + ".XXXXXX";
    const int fd = ::mkstemp(tmp.data());  // mode 0600
    if (fd < 0) {
        error = path + ": no se puede escribir: " + std::strerror(errno);
        return false;
    }
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    std::FILE* f = ::fdopen(fd, "wb");
    bool ok = f != nullptr && write_all(f, data) && ::fsync(fd) == 0;
    if (f != nullptr) {
        if (std::fclose(f) != 0) ok = false;
    } else {
        ::close(fd);
    }
    if (!ok) {
        error = path + ": no se puede escribir";
        fs::remove(tmp, ec);
        return false;
    }
#endif
    fs::rename(tmp, path, ec);
    if (ec) {
        error = path + ": no se puede escribir: " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string hash_token(std::string_view token) { return fetch::sha256_hex(token); }

std::string new_token() {
    static const char digits[] = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    for (int i = 0; i < 8; ++i) {
        std::uint32_t v = static_cast<std::uint32_t>(rd());
        for (int k = 0; k < 4; ++k) {
            const unsigned byte = v & 0xffu;
            v >>= 8;
            out += digits[byte >> 4];
            out += digits[byte & 0x0fu];
        }
    }
    return out;
}

std::string now_utc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

Change add(const std::string& path, std::string_view name) {
    Change out;
    if (!valid_name(name)) {
        out.status = Status::InvalidName;
        return out;
    }
    LoadResult loaded = load(path);
    if (!loaded.entries) return {Status::FileError, {}, loaded.error};
    for (const Entry& e : *loaded.entries) {
        if (e.name == name) {
            out.status = Status::Duplicate;
            return out;
        }
    }
    const std::string token = new_token();
    loaded.entries->push_back({hash_token(token), std::string(name), now_utc()});
    if (!save(path, *loaded.entries, out.error)) {
        out.status = Status::FileError;
        return out;
    }
    out.token = token;
    return out;
}

Change revoke(const std::string& path, std::string_view name) {
    Change out;
    if (!valid_name(name)) {
        out.status = Status::InvalidName;
        return out;
    }
    LoadResult loaded = load(path);
    if (!loaded.entries) return {Status::FileError, {}, loaded.error};
    std::vector<Entry>& entries = *loaded.entries;
    const auto before = entries.size();
    std::erase_if(entries, [&](const Entry& e) { return e.name == name; });
    if (entries.size() == before) {
        out.status = Status::NotFound;
        return out;
    }
    if (!save(path, entries, out.error)) out.status = Status::FileError;
    return out;
}

bool verify(const std::vector<Entry>& entries, std::string_view token) {
    const std::string hash = hash_token(token);
    unsigned match = 0;
    for (const Entry& e : entries) {
        if (e.hash.size() != hash.size()) continue;  // never for a parsed file
        unsigned diff = 0;
        for (std::size_t i = 0; i < hash.size(); ++i)
            diff |= static_cast<unsigned>(static_cast<unsigned char>(e.hash[i]) ^ static_cast<unsigned char>(hash[i]));
        match |= static_cast<unsigned>(diff == 0);
    }
    return match != 0;
}

std::string unique_name(const std::vector<Entry>& entries, std::string_view wanted) {
    std::string base;
    for (const char c : wanted) {
        if (base.size() == kMaxNameLength) break;
        base += is_name_char(c) ? c : '-';
    }
    if (base.empty()) base = "cliente";
    const auto taken = [&](const std::string& n) {
        for (const Entry& e : entries)
            if (e.name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (std::size_t i = 2;; ++i) {
        const std::string suffix = "-" + std::to_string(i);
        const std::string name = base.substr(0, kMaxNameLength - suffix.size()) + suffix;
        if (!taken(name)) return name;
    }
}

}  // namespace revisa::tokens
