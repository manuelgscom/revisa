#pragma once
// Model download with the system curl (or wget) and SHA-256 check. revisa's only network request,
// and only when the user accepts it.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace revisa::fetch {

// Directory of the downloaded models. Empty if it cannot be determined.
//   Linux:   $XDG_DATA_HOME/revisa/models, else $HOME/.local/share/revisa/models
//   Windows: %LOCALAPPDATA%\revisa\models
std::string models_dir();

struct DownloadResult {
    enum class Status {
        Ok,
        Interrupted,  // Ctrl-C during the download: the CLI exits with 130
        Cancelled,    // on_progress returned false
        Failed,
    };
    Status status = Status::Failed;
    std::string path;   // final path if Ok
    std::string error;  // Spanish
};

struct DownloadOptions {
    // Free bytes on dir's filesystem. Defaults to statvfs; tests override it.
    // nullopt = unknown (does not block the download).
    std::function<std::optional<std::uint64_t>(const std::string& dir)> free_space;
    // PATH in which to look for curl/wget. Empty = the environment's PATH.
    std::string path_env;
    // Show the tool's progress bar (stderr is a terminal).
    bool progress = false;
    // Called about every 200 ms with the bytes downloaded so far (size of the temp file).
    // Returning false kills the tool, removes the temp file and returns Cancelled.
    // Called from the thread that runs download().
    std::function<bool(std::uint64_t bytes_done)> on_progress;
    // Ignore SIGINT/SIGQUIT (process-wide) while the tool runs, so that Ctrl-C only stops the
    // tool (CLI). A GUI sets false and cancels through on_progress.
    bool ignore_interrupts = true;
};

// Creates dir (0755, with parents), downloads url to a temporary file in dir, checks size and
// SHA-256 and renames it to dir/filename. On any failure neither the temp nor the final file remains.
DownloadResult download(const std::string& url, const std::string& dir, const std::string& filename,
                        std::uint64_t size, const std::string& sha256, const DownloadOptions& options = {});

}  // namespace revisa::fetch
