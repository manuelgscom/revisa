#include "core/fetch/fetch.hpp"

#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/fetch/posix_util.hpp"
#include "core/fetch/sha256.hpp"
#else
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <random>
#include <vector>

#include "core/fetch/sha256.hpp"
#endif

namespace revisa::fetch {

namespace {

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

// Strips trailing separators ("/home/x/" -> "/home/x"; "/" -> "").
std::string strip_trailing_slashes(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

DownloadResult failed(std::string error) {
    DownloadResult r;
    r.status = DownloadResult::Status::Failed;
    r.error = std::move(error);
    return r;
}

#ifndef _WIN32

bool is_dir(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// mkdir -p with 0755.
bool make_dirs(const std::string& dir) {
    if (dir.empty()) return false;
    for (std::size_t pos = 1; pos <= dir.size(); ++pos) {
        if (pos < dir.size() && dir[pos] != '/') continue;
        const std::string part = dir.substr(0, pos);
        if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return is_dir(dir);
}

std::optional<std::uint64_t> statvfs_free(const std::string& dir) {
    struct statvfs st {};
    if (statvfs(dir.c_str(), &st) != 0) return std::nullopt;
    return std::uint64_t{st.f_bavail} * std::uint64_t{st.f_frsize};
}

// Absolute path of the first executable prog in path; empty if not found.
std::string find_in_path(const std::string& path, const std::string& prog) {
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            const std::string full = dir + "/" + prog;
            struct stat st {};
            if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(full.c_str(), X_OK) == 0) return full;
        }
        start = end + 1;
    }
    return {};
}

std::string mib(std::uint64_t bytes) { return std::to_string((bytes + (1 << 20) - 1) >> 20); }

bool fsync_path(const std::string& path, int flags) {
    const int fd = open(path.c_str(), flags | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

#else  // _WIN32

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string mib(std::uint64_t bytes) { return std::to_string((bytes + (1 << 20) - 1) >> 20); }

bool is_regular_file(const std::string& path) {
    const DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::optional<std::uint64_t> file_size(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &d)) return std::nullopt;
    return (std::uint64_t{d.nFileSizeHigh} << 32) | d.nFileSizeLow;
}

std::optional<std::uint64_t> disk_free(const std::string& dir) {
    ULARGE_INTEGER avail{};
    if (!GetDiskFreeSpaceExW(widen(dir).c_str(), &avail, nullptr, nullptr)) return std::nullopt;
    return std::uint64_t{avail.QuadPart};
}

bool make_dirs(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    const std::filesystem::path p(widen(dir));
    std::filesystem::create_directories(p, ec);
    return std::filesystem::is_directory(p, ec);
}

// curl.exe: only in path_env (';'-separated) when given (tests); otherwise the one Windows 10/11
// ships in System32 first, then PATH. Empty if not found.
std::string find_curl(const std::string& path_env) {
    const auto search = [](const std::string& list) -> std::string {
        std::size_t start = 0;
        while (start <= list.size()) {
            std::size_t end = list.find(';', start);
            if (end == std::string::npos) end = list.size();
            const std::string dir = strip_trailing_slashes(list.substr(start, end - start));
            if (!dir.empty() && is_regular_file(dir + "\\curl.exe")) return dir + "\\curl.exe";
            start = end + 1;
        }
        return {};
    };
    if (!path_env.empty()) return search(path_env);
    const std::string root = strip_trailing_slashes(env_or_empty("SystemRoot"));
    if (!root.empty() && is_regular_file(root + "\\System32\\curl.exe")) return root + "\\System32\\curl.exe";
    return search(env_or_empty("PATH"));
}

// One argument quoted for CommandLineToArgvW / the CRT parser.
std::wstring quote_arg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
        else out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// Creates an empty, unique dir\.<filename>.part-XXXXXX. Empty on failure.
std::string create_temp(const std::string& dir, const std::string& filename) {
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> pick(0, static_cast<int>(sizeof kChars) - 2);
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string tmp = dir + "\\." + filename + ".part-";
        for (int i = 0; i < 6; ++i) tmp += kChars[pick(gen)];
        const HANDLE h = CreateFileW(widen(tmp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return tmp;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) return {};
    }
    return {};
}

// While alive, Ctrl-C/Ctrl-Break do not end this process: only curl, which shares the console,
// dies (exit code STATUS_CONTROL_C_EXIT). A handler function, not SetConsoleCtrlHandler(nullptr),
// whose "ignore" flag would be inherited by curl.
BOOL WINAPI swallow_ctrl_c(DWORD type) { return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ? TRUE : FALSE; }

struct IgnoreInterrupts {
    IgnoreInterrupts() { SetConsoleCtrlHandler(swallow_ctrl_c, TRUE); }
    ~IgnoreInterrupts() { SetConsoleCtrlHandler(swallow_ctrl_c, FALSE); }
    IgnoreInterrupts(const IgnoreInterrupts&) = delete;
    IgnoreInterrupts& operator=(const IgnoreInterrupts&) = delete;
};

#endif  // _WIN32

}  // namespace

std::string models_dir() {
#ifdef _WIN32
    const std::string local = strip_trailing_slashes(env_or_empty("LOCALAPPDATA"));
    if (!local.empty()) return local + "\\revisa\\models";
    return {};
#else
    const std::string xdg = strip_trailing_slashes(env_or_empty("XDG_DATA_HOME"));
    if (!xdg.empty()) return xdg + "/revisa/models";
    const std::string home = strip_trailing_slashes(env_or_empty("HOME"));
    if (!home.empty()) return home + "/.local/share/revisa/models";
    return {};
#endif
}

#ifdef _WIN32

// Same steps and messages as the POSIX version below, with curl.exe (Windows 10/11 ships it) run
// through CreateProcessW. No wget fallback.
DownloadResult download(const std::string& url, const std::string& dir, const std::string& filename,
                        std::uint64_t size, const std::string& sha256, const DownloadOptions& options) {
    // 1. Directory.
    if (!make_dirs(dir)) return failed("no se pudo crear el directorio " + dir);

    // 2. Free space: size + 10%.
    const std::uint64_t needed = size + size / 10;
    const std::optional<std::uint64_t> avail = options.free_space ? options.free_space(dir) : disk_free(dir);
    if (avail && *avail < needed) {
        return failed("no hay espacio suficiente en " + dir + ": hacen falta " + mib(needed) + " MiB y hay " +
                      mib(*avail) + " MiB libres");
    }

    // 3. curl.exe (before the temp file: if it does not exist nothing is created).
    const std::string tool = find_curl(options.path_env);
    if (tool.empty()) {
        return failed("no se encuentra curl; descarga el modelo a mano desde " + url + " (SHA-256 " + sha256 +
                      ") y usa --model <fichero>");
    }

    // 4. Temporary file in the same directory.
    const std::string tmp = create_temp(dir, filename);
    if (tmp.empty()) return failed("no se pudo crear el fichero temporal en " + dir);
    const auto remove_tmp = [&] { DeleteFileW(widen(tmp).c_str()); };

    // 5. Fixed argv, no shell; the resolved path is the application name.
    std::vector<std::string> argv = {tool,  "-fL", "--proto", "=https", "--tlsv1.2", "--retry", "3",
                                     "--connect-timeout", "20"};
    argv.emplace_back(options.progress ? "--progress-bar" : "-sS");
    argv.insert(argv.end(), {"-o", tmp, url});
    std::wstring cmdline;
    for (const auto& a : argv) {
        if (!cmdline.empty()) cmdline += L' ';
        cmdline += quote_arg(widen(a));
    }

    // 6. Wait polling every 200 ms (progress and cancellation). Without a console of our own (the
    //    tray app) curl gets none either, so no window flashes.
    int code = 0;
    bool cancelled = false;
    {
        std::fflush(nullptr);
        std::optional<IgnoreInterrupts> guard;
        if (options.ignore_interrupts) guard.emplace();
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        const DWORD flags = GetConsoleWindow() == nullptr ? CREATE_NO_WINDOW : 0;
        if (!CreateProcessW(widen(tool).c_str(), cmdline.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr,
                            &si, &pi)) {
            code = -1;
        } else {
            CloseHandle(pi.hThread);
            while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT) {
                if (!options.on_progress) continue;
                if (!options.on_progress(file_size(tmp).value_or(0))) {
                    TerminateProcess(pi.hProcess, 1);
                    WaitForSingleObject(pi.hProcess, INFINITE);
                    cancelled = true;
                    break;
                }
            }
            DWORD exit_code = 1;
            GetExitCodeProcess(pi.hProcess, &exit_code);
            CloseHandle(pi.hProcess);
            code = static_cast<int>(exit_code);
        }
    }
    if (cancelled) {
        remove_tmp();
        DownloadResult r;
        r.status = DownloadResult::Status::Cancelled;
        r.error = "descarga cancelada";
        return r;
    }
    if (code != 0) {
        remove_tmp();
        if (static_cast<DWORD>(code) == STATUS_CONTROL_C_EXIT) {
            DownloadResult r;
            r.status = DownloadResult::Status::Interrupted;
            r.error = "descarga interrumpida";
            return r;
        }
        if (code == -1) return failed("no se pudo lanzar curl");
        return failed("la descarga ha fallado: curl salió con código " + std::to_string(code));
    }

    // 7. Exact size and SHA-256.
    const std::optional<std::uint64_t> got = file_size(tmp);
    if (!got || *got != size || sha256_file(tmp) != sha256) {
        remove_tmp();
        return failed("el fichero descargado no es íntegro");
    }

    // 8. Rename into place (written through to disk before returning).
    const std::string final_path = dir + "\\" + filename;
    if (!MoveFileExW(widen(tmp).c_str(), widen(final_path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove_tmp();
        return failed("no se pudo guardar el modelo en " + final_path);
    }

    DownloadResult r;
    r.status = DownloadResult::Status::Ok;
    r.path = final_path;
    return r;
}

#else

DownloadResult download(const std::string& url, const std::string& dir, const std::string& filename,
                        std::uint64_t size, const std::string& sha256, const DownloadOptions& options) {
    // 1. Directory.
    if (!make_dirs(dir)) return failed("no se pudo crear el directorio " + dir);

    // 2. Free space: size + 10%.
    const std::uint64_t needed = size + size / 10;
    const std::optional<std::uint64_t> avail = options.free_space ? options.free_space(dir) : statvfs_free(dir);
    if (avail && *avail < needed) {
        return failed("no hay espacio suficiente en " + dir + ": hacen falta " + mib(needed) + " MiB y hay " +
                      mib(*avail) + " MiB libres");
    }

    // 3. curl, or else wget (before the temp file: if neither exists nothing is created).
    const std::string path_env = options.path_env.empty() ? env_or_empty("PATH") : options.path_env;
    std::string tool = find_in_path(path_env, "curl");
    const bool use_curl = !tool.empty();
    if (!use_curl) tool = find_in_path(path_env, "wget");
    if (tool.empty()) {
        return failed("no se encuentra curl ni wget; descarga el modelo a mano desde " + url + " (SHA-256 " + sha256 +
                      ") y usa --model <fichero>");
    }
    const std::string tool_name = use_curl ? "curl" : "wget";

    // 4. Temporary file in the same directory.
    const std::string tmpl = dir + "/." + filename + ".part-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const int fd = mkstemp(buf.data());
    if (fd < 0) return failed("no se pudo crear el fichero temporal en " + dir);
    close(fd);  // the tool reopens it by name
    const std::string tmp(buf.data());

    // 5. Fixed argv, no shell. Runs the resolved path (execvp with a '/' does not search PATH).
    std::vector<std::string> argv;
    if (use_curl) {
        argv = {tool, "-fL", "--proto", "=https", "--tlsv1.2", "--retry", "3", "--connect-timeout", "20"};
        argv.emplace_back(options.progress ? "--progress-bar" : "-sS");
        argv.insert(argv.end(), {"-o", tmp, url});
    } else {
        argv = {tool, "--https-only"};
        if (options.progress) argv.insert(argv.end(), {"-q", "--show-progress"});
        else argv.emplace_back("-q");
        argv.insert(argv.end(), {"-O", tmp, url});
    }

    // 6. Wait polling every 200 ms (progress and cancellation). By default the parent ignores
    //    SIGINT while waiting; the child keeps the default handler.
    int code = 0;
    bool cancelled = false;
    {
        std::fflush(nullptr);
        std::optional<detail::IgnoreInterrupts> guard;
        if (options.ignore_interrupts) guard.emplace();
        const pid_t pid = detail::spawn(argv);
        if (pid < 0) code = -1;
        while (pid >= 0 && !detail::try_wait_child(pid, code)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!options.on_progress) continue;
            struct stat st {};
            const std::uint64_t done = stat(tmp.c_str(), &st) == 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
            if (!options.on_progress(done)) {
                detail::kill_child(pid);
                cancelled = true;
                break;
            }
        }
    }
    if (cancelled) {
        unlink(tmp.c_str());
        DownloadResult r;
        r.status = DownloadResult::Status::Cancelled;
        r.error = "descarga cancelada";
        return r;
    }
    if (code != 0) {
        unlink(tmp.c_str());
        if (code == 128 + SIGINT) {
            DownloadResult r;
            r.status = DownloadResult::Status::Interrupted;
            r.error = "descarga interrumpida";
            return r;
        }
        if (code < 0) return failed("no se pudo lanzar " + tool_name);
        if (code > 128) {
            return failed("la descarga ha fallado: " + tool_name + " terminó por la señal " +
                          std::to_string(code - 128));
        }
        return failed("la descarga ha fallado: " + tool_name + " salió con código " + std::to_string(code));
    }

    // 7. Exact size and SHA-256.
    struct stat st {};
    const bool size_ok =
        stat(tmp.c_str(), &st) == 0 && S_ISREG(st.st_mode) && static_cast<std::uint64_t>(st.st_size) == size;
    if (!size_ok || sha256_file(tmp) != sha256) {
        unlink(tmp.c_str());
        return failed("el fichero descargado no es íntegro");
    }

    // 8. fsync, rename and fsync of the directory. mkstemp creates with 0600; the model is not secret.
    const std::string final_path = dir + "/" + filename;
    if (chmod(tmp.c_str(), 0644) != 0 || !fsync_path(tmp, O_RDWR) || rename(tmp.c_str(), final_path.c_str()) != 0) {
        unlink(tmp.c_str());
        return failed("no se pudo guardar el modelo en " + final_path);
    }
    fsync_path(dir, O_RDONLY | O_DIRECTORY);  // the file is in place; a failure here is not rolled back

    DownloadResult r;
    r.status = DownloadResult::Status::Ok;
    r.path = final_path;
    return r;
}

#endif  // _WIN32

}  // namespace revisa::fetch
