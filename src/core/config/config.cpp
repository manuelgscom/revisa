#include "core/config/config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace revisa::config {

namespace {

#ifdef _WIN32
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

// Strips trailing separators ("/home/x/" -> "/home/x"; "/" -> "").
std::string_view strip_trailing_slashes(std::string_view s) {
    while (!s.empty() && (s.back() == '/' || s.back() == kSep)) s.remove_suffix(1);
    return s;
}

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

// The directory "~/" expands to.
std::string home_dir() {
#ifdef _WIN32
    return env_or_empty("USERPROFILE");
#else
    return env_or_empty("HOME");
#endif
}

void append_hex(std::string& out, unsigned char c) {
    static const char digits[] = "0123456789abcdef";
    out += "\\x";
    out += digits[c >> 4];
    out += digits[c & 0x0f];
}

// Length of the valid UTF-8 sequence starting at s[i], or 0 if invalid.
// C1 controls (U+0080..U+009F) count as invalid: they get escaped.
std::size_t utf8_len(std::string_view s, std::size_t i) {
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    std::size_t n = 0;
    unsigned cp = 0;
    if (c >= 0xc2 && c <= 0xdf) {
        n = 2;
        cp = c & 0x1fu;
    } else if (c >= 0xe0 && c <= 0xef) {
        n = 3;
        cp = c & 0x0fu;
    } else if (c >= 0xf0 && c <= 0xf4) {
        n = 4;
        cp = c & 0x07u;
    } else {
        return 0;
    }
    if (i + n > s.size()) return 0;
    for (std::size_t k = 1; k < n; ++k) {
        if ((b(i + k) & 0xc0) != 0x80) return 0;
        cp = (cp << 6) | (b(i + k) & 0x3fu);
    }
    if (cp < 0xa0) return 0;  // C1 or overlong form
    if (n == 3 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) return 0;
    if (n == 4 && (cp < 0x10000 || cp > 0x10ffff)) return 0;
    return n;
}

// Quoted value for a message: no control characters that could reach the terminal.
std::string quote(std::string_view s) {
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
            ++i;
        } else if (c < 0x20 || c == 0x7f) {
            append_hex(out, c);
            ++i;
        } else if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
        } else if (const std::size_t n = utf8_len(s, i); n > 0) {
            out.append(s.substr(i, n));
            i += n;
        } else {
            append_hex(out, c);
            ++i;
        }
    }
    out += '"';
    return out;
}

ConfigResult fail(std::string_view filename, int line, const std::string& reason) {
    return {std::nullopt, std::string(filename) + ":" + std::to_string(line) + ": " + reason};
}

// Integer in [lo, hi]: digits only, no sign; leading zeros do not count towards the length.
bool parse_int(std::string_view v, int lo, int hi, int& out) {
    if (v.empty()) return false;
    long n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
        if (n > hi) return false;  // stop before overflowing
    }
    if (n < lo) return false;
    out = static_cast<int>(n);
    return true;
}

constexpr const char* kKeys[] = {"model", "threads", "disabled_types", "port", "hotkey"};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
constexpr const char* kKeyList = "model, threads, disabled_types, port, hotkey";

bool has_control(std::string_view v) {
    for (char ch : v) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

// Catalog ids separated by commas and/or spaces. Returns the first unknown id, if any.
std::optional<std::string_view> parse_types(std::string_view v, std::set<catalog::Type>& out) {
    std::size_t i = 0;
    while (i < v.size()) {
        const auto sep = [](char c) { return c == ',' || c == ' ' || c == '\t'; };
        while (i < v.size() && sep(v[i])) ++i;
        std::size_t j = i;
        while (j < v.size() && !sep(v[j])) ++j;
        if (j > i) {
            const std::string_view id = v.substr(i, j - i);
            const auto t = catalog::type_from_id(id);
            if (!t) return id;
            out.insert(*t);
        }
        i = j;
    }
    return std::nullopt;
}

// --- save_config ------------------------------------------------------------------------------

// The written form of key k in cfg; nullopt when the key has none and its line is omitted (model
// empty, threads 0 = automatic, no disabled types).
std::optional<std::string> value_of(int k, const Config& cfg) {
    switch (k) {
        case 0: return cfg.model.empty() ? std::nullopt : std::optional<std::string>(cfg.model);
        case 1: return cfg.threads == 0 ? std::nullopt : std::optional<std::string>(std::to_string(cfg.threads));
        case 2: {
            if (cfg.disabled_types.empty()) return std::nullopt;
            std::string out;
            for (const catalog::Type t : cfg.disabled_types) {
                if (!out.empty()) out += ", ";
                out += catalog::type_id(t);
            }
            return out;
        }
        case 3: return std::to_string(cfg.port);
        default: return cfg.hotkey;
    }
}

bool same_value(int k, const Config& a, const Config& b) {
    switch (k) {
        case 0: return a.model == b.model;
        case 1: return a.threads == b.threads;
        case 2: return a.disabled_types == b.disabled_types;
        case 3: return a.port == b.port;
        default: return a.hotkey == b.hotkey;
    }
}

int key_index(std::string_view key) {
    for (int i = 0; i < kKeyCount; ++i)
        if (key == kKeys[i]) return i;
    return -1;
}

bool set_error(std::string* error, std::string msg) {
    if (error != nullptr) *error = std::move(msg);
    return false;
}

// Writes data to a temporary file next to path and renames it over path.
bool write_atomic(const std::filesystem::path& path, const std::string& data, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = path.parent_path();
    if (!dir.empty() && !fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        if (ec) return set_error(error, dir.string() + ": no se puede crear: " + ec.message());
    }
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) {
            fs::remove(tmp, ec);
            return set_error(error, tmp.string() + ": no se puede escribir");
        }
    }
#ifndef _WIN32
    if (const int fd = ::open(tmp.c_str(), O_RDONLY); fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
    // Keep the permissions of the file being replaced.
    if (const fs::file_status st = fs::status(path, ec); !ec && fs::exists(st))
        fs::permissions(tmp, st.permissions(), ec);
    fs::rename(tmp, path, ec);
    if (ec) {
        const std::string why = ec.message();
        fs::remove(tmp, ec);
        return set_error(error, path.string() + ": no se puede escribir: " + why);
    }
    return true;
}

}  // namespace

ConfigResult parse_config(std::string_view text, std::string_view filename, std::string_view home) {
    Config cfg;
    int seen_line[kKeyCount] = {};

    // Leading UTF-8 BOM: ignored.
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);

    int lineno = 0;
    while (!text.empty()) {
        ++lineno;
        const std::size_t nl = text.find('\n');
        const std::string_view raw = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);

        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#') continue;

        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return fail(filename, lineno, "falta \"=\" (formato: clave = valor)");

        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));

        int k = -1;
        for (int i = 0; i < kKeyCount; ++i)
            if (key == kKeys[i]) k = i;
        if (k < 0)
            return fail(filename, lineno, "clave desconocida: " + quote(key) + " (válidas: " + kKeyList + ")");

        const std::string name = kKeys[k];
        if (seen_line[k] != 0)
            return fail(filename, lineno,
                        "clave repetida: " + name + " (ya definida en la línea " + std::to_string(seen_line[k]) + ")");
        seen_line[k] = lineno;

        if (value.empty()) return fail(filename, lineno, "valor vacío para " + name);

        if (name == "model") {
            if (value.starts_with("~/")) {
                const std::string_view base = strip_trailing_slashes(home);
                if (base.empty())
                    return fail(filename, lineno, "no se puede expandir \"~/\" en model: HOME no está definida");
                cfg.model = std::string(base) + std::string(value.substr(1));
            } else {
                cfg.model = std::string(value);
            }
        } else if (name == "disabled_types") {
            if (const auto bad = parse_types(value, cfg.disabled_types))
                return fail(filename, lineno,
                            "tipo desconocido en disabled_types: " + quote(*bad) +
                                " (válidos: los ids del catálogo, como MAYUSCULA o ESPACIADO)");
            if (cfg.disabled_types.empty()) return fail(filename, lineno, "valor vacío para " + name);
        } else if (name == "port") {
            if (!parse_int(value, 1, 65535, cfg.port))
                return fail(filename, lineno,
                            "valor no válido para port: " + quote(value) + " (válidos: entero de 1 a 65535)");
        } else if (name == "hotkey") {
            if (has_control(value))
                return fail(filename, lineno,
                            "valor no válido para hotkey: " + quote(value) +
                                " (válidos: un atajo como Ctrl+Alt+Space, sin caracteres de control)");
            cfg.hotkey = std::string(value);
        } else {
            if (!parse_int(value, 1, kMaxThreads, cfg.threads))
                return fail(filename, lineno,
                            "valor no válido para threads: " + quote(value) + " (válidos: entero de 1 a " +
                                std::to_string(kMaxThreads) + ")");
        }
    }
    return {cfg, {}};
}

std::string config_path() {
#ifdef _WIN32
    const std::string appdata = env_or_empty("APPDATA");
    if (appdata.empty()) return {};
    return std::string(strip_trailing_slashes(appdata)) + "\\revisa\\config";
#else
    const std::string xdg = env_or_empty("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        const std::string_view base = strip_trailing_slashes(xdg);
        return std::string(base) + "/revisa/config";
    }
    const std::string home = env_or_empty("HOME");
    if (home.empty()) return {};
    return std::string(strip_trailing_slashes(home)) + "/.config/revisa/config";
#endif
}

ConfigResult load_config() {
    const std::string path = config_path();
    if (path.empty()) return {Config{}, {}};

    const std::string home = home_dir();

    // Display name: $HOME/... -> ~/...
    std::string shown = path;
    const std::string_view base = strip_trailing_slashes(home);
    if (!base.empty() && path.size() > base.size() + 1 && path.compare(0, base.size(), base) == 0 &&
        path[base.size()] == kSep)
        shown = "~" + path.substr(base.size());

    const auto unreadable = [&](const std::string& why) {
        return ConfigResult{std::nullopt, shown + ": no se puede leer: " + why};
    };

    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return unreadable("es un directorio");

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        if (errno == ENOENT) return {Config{}, {}};
        return unreadable(std::strerror(errno));
    }
    std::string text;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    const bool read_error = std::ferror(f) != 0;
    std::fclose(f);
    if (read_error) return unreadable("error de lectura");

    return parse_config(text, shown, home);
}

bool save_config(const std::string& path, const Config& cfg, std::string* error) {
    namespace fs = std::filesystem;
    if (path.empty()) return set_error(error, "no se sabe dónde guardar la configuración");
    const std::string home = home_dir();

    // What the file must read back as: the hotkey trimmed, "~/" expanded as the parser does.
    Config want = cfg;
    want.hotkey = std::string(trim(cfg.hotkey));
    if (want.model.starts_with("~/") && !strip_trailing_slashes(home).empty())
        want.model = std::string(strip_trailing_slashes(home)) + want.model.substr(1);

    // A symbolic link (dotfiles) is followed: its target is rewritten.
    std::error_code ec;
    fs::path target = path;
    if (fs::is_symlink(target, ec)) {
        target = fs::weakly_canonical(target, ec);
        if (ec) return set_error(error, path + ": no se puede leer: " + ec.message());
    }
    if (fs::is_directory(target, ec)) return set_error(error, path + ": no se puede leer: es un directorio");

    std::string text;
    const bool existed = fs::exists(target, ec);
    if (existed) {
        std::ifstream in(target, std::ios::binary);
        if (!in) return set_error(error, path + ": no se puede leer");
        std::ostringstream buf;
        buf << in.rdbuf();
        if (in.bad()) return set_error(error, path + ": no se puede leer: error de lectura");
        text = buf.str();
    }
    const ConfigResult old = parse_config(text, path, home);
    if (!old.config) return set_error(error, old.error);

    // Rewrite the lines with a key; everything else is copied as is.
    std::string out;
    out.reserve(text.size() + 64);
    bool present[kKeyCount] = {};
    bool crlf = false;
    std::string_view rest = text;
    if (rest.substr(0, 3) == "\xEF\xBB\xBF") {
        out += rest.substr(0, 3);
        rest.remove_prefix(3);
    }
    while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        const std::string_view raw = rest.substr(0, nl);  // without the '\n'
        const bool has_nl = nl != std::string_view::npos;
        rest.remove_prefix(has_nl ? nl + 1 : rest.size());
        const bool cr = !raw.empty() && raw.back() == '\r';
        crlf = crlf || cr;

        const std::string_view line = trim(raw);
        const int k = line.empty() || line.front() == '#' ? -1 : key_index(trim(line.substr(0, line.find('='))));
        if (k >= 0) present[k] = true;
        if (k < 0 || same_value(k, *old.config, want)) {
            out += raw;
            if (has_nl) out += '\n';
            continue;
        }
        const std::optional<std::string> v = value_of(k, want);
        if (!v) continue;  // back to the default with no written form: the line goes
        out += std::string(kKeys[k]) + " = " + *v;
        if (cr) out += '\r';
        if (has_nl) out += '\n';
    }
    for (int k = 0; k < kKeyCount; ++k) {
        if (present[k] || same_value(k, *old.config, want)) continue;
        const std::optional<std::string> v = value_of(k, want);
        if (!v) continue;
        if (!out.empty() && out.back() != '\n') out += crlf ? "\r\n" : "\n";
        out += std::string(kKeys[k]) + " = " + *v + (crlf ? "\r\n" : "\n");
    }

    // The file must read back as want: a value the parser rejects or reads differently (control
    // characters, spaces at the ends, threads or port out of range…) is not written.
    const ConfigResult back = parse_config(out, path, home);
    if (!back.config) return set_error(error, "valor no válido: " + back.error);
    for (int k = 0; k < kKeyCount; ++k)
        if (!same_value(k, *back.config, want))
            return set_error(error, std::string("valor no válido para ") + kKeys[k]);

    if (existed && out == text) return true;
    return write_atomic(target, out, error);
}

}  // namespace revisa::config
