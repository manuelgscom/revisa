#include "cli/cli.hpp"

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <string>
#include <utility>

#include "cli/args.hpp"
#include "cli/commands.hpp"
#include "cli/messages.hpp"
#include "cli/report.hpp"
#include "core/check/check.hpp"
#include "core/classify/classify.hpp"
#include "core/config/config.hpp"
#include "core/fetch/fetch.hpp"
#include "core/fetch/model_resolve.hpp"
#include "core/guard/guard.hpp"
#include "core/infer/cpu.hpp"
#include "core/infer/engine.hpp"
#include "core/infer/llama_engine.hpp"
#include "core/infer/prompt.hpp"
#include "core/infer/replay_engine.hpp"
#if REVISA_TEST_HOOKS
#include "core/infer/fake_engine.hpp"
#endif

namespace revisa::cli {

using fetch::default_model_spec;
using fetch::model_name;
using fetch::ModelResolution;
using fetch::ModelSpec;
using fetch::resolve_model;
using fetch::size_mb;

namespace {

// --- The real process ----------------------------------------------------------------------

// Raw descriptor I/O. On Windows the CRT's _read/_write take an unsigned count and return int;
// the descriptors are switched to binary mode in run() so bytes pass through unchanged.
#ifdef _WIN32
using ssize_t = long long;
ssize_t sys_write(int fd, const void* buf, std::size_t n) {
    return _write(fd, buf, static_cast<unsigned>(n > 0x40000000 ? 0x40000000 : n));
}
ssize_t sys_read(int fd, void* buf, std::size_t n) { return _read(fd, buf, static_cast<unsigned>(n)); }
int sys_open_read(const char* path) { return _open(path, _O_RDONLY | _O_BINARY | _O_NOINHERIT); }
int sys_close(int fd) { return _close(fd); }
bool sys_isatty(int fd) { return _isatty(fd) != 0; }
#else
ssize_t sys_write(int fd, const void* buf, std::size_t n) { return ::write(fd, buf, n); }
ssize_t sys_read(int fd, void* buf, std::size_t n) { return ::read(fd, buf, n); }
int sys_open_read(const char* path) { return ::open(path, O_RDONLY | O_CLOEXEC); }
int sys_close(int fd) { return ::close(fd); }
bool sys_isatty(int fd) { return ::isatty(fd) == 1; }
#endif

void write_fd(int fd, std::string_view text) {
    while (!text.empty()) {
        const ssize_t n = sys_write(fd, text.data(), text.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        text.remove_prefix(static_cast<std::size_t>(n));
    }
}

// Short Spanish reason for an errno of open/read.
std::string errno_reason(int e) {
    switch (e) {
        case ENOENT: return "no existe";
        case EACCES:
        case EPERM: return "permiso denegado";
        case EISDIR: return "es un directorio";
        case ENOTDIR: return "una parte de la ruta no es un directorio";
        default: return "error de lectura (errno " + std::to_string(e) + ")";
    }
}

// Reads fd to the end. nullopt = read error (reason in error).
std::optional<std::string> read_all(int fd, std::string& error) {
    std::string text;
    char buf[65536];
    for (;;) {
        const ssize_t n = sys_read(fd, buf, sizeof buf);
        if (n > 0) {
            text.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
            return text;
        } else if (errno != EINTR) {
            error = errno_reason(errno);
            return std::nullopt;
        }
    }
}

std::optional<std::string> read_file(const std::string& path, std::string& error) {
    const int fd = sys_open_read(path.c_str());
    if (fd < 0) {
        error = errno_reason(errno);
        return std::nullopt;
    }
    auto text = read_all(fd, error);
    sys_close(fd);
    return text;
}

// One stdin line, read byte by byte so that nothing after it is consumed. nullopt = EOF with
// nothing read.
std::optional<std::string> read_stdin_line() {
    std::string line;
    for (;;) {
        char c = 0;
        const ssize_t n = sys_read(0, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }
        if (n == 0) {
            if (line.empty()) return std::nullopt;
            return line;
        }
        if (c == '\n') return line;
        line += c;
    }
}

// Ctrl-C: a newline (on stderr, so that a piped stdout stays clean) and death by SIGINT, so that
// the parent shell sees it (130). Async-signal-safe functions only.
#ifdef _WIN32
// Windows runs console control handlers on their own thread and SIGINT's default action exits
// with 3: exit with 130 directly, as a POSIX shell would report.
BOOL WINAPI on_console_ctrl(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    sys_write(2, "\n", 1);
    _exit(130);
}
#else
extern "C" void on_sigint(int) {
    if (::write(2, "\n", 1) < 0) {
    }
    std::signal(SIGINT, SIG_DFL);
    std::raise(SIGINT);
}
#endif

// --- Model ---------------------------------------------------------------------------------

// Enter (or s/sí/y/yes) = yes; anything else or EOF = no.
bool is_yes(const std::string& line) {
    std::string a;
    for (const char c : line)
        if (c != ' ' && c != '\t' && c != '\r') a += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return a.empty() || a == "s" || a == "si" || a == "sí" || a == "sÍ" || a == "y" || a == "yes";
}

// The model path, or the exit code (with the message already written).
struct ModelPick {
    std::string path;
    int exit_code = -1;
};

ModelPick pick_model(const std::string& explicit_path, const ModelSpec& spec, bool json, const Host& host) {
    const std::string dir = fetch::models_dir();
    const ModelResolution res = resolve_model(explicit_path, dir, spec);
    switch (res.status) {
        case ModelResolution::Status::Ready:
            return {res.path, -1};
        case ModelResolution::Status::Missing:
            host.err(msg(Msg::ModelMissing, {res.path}));
            return {{}, kExitError};
        case ModelResolution::Status::NoDir:
            host.err(msg(Msg::NoModelsDir));
            return {{}, kExitError};
        case ModelResolution::Status::NeedsDownload:
            break;
    }
    const std::string mb = std::to_string(size_mb(spec.size));
    if (json || !host.stdin_tty || !host.stderr_tty) {
        host.err(msg(Msg::DownloadNoTty, {spec.name, mb, spec.url}));
        return {{}, kExitError};
    }
    host.err(msg(Msg::DownloadPrompt, {spec.name, mb, spec.url}));
    const auto line = host.read_line ? host.read_line() : std::nullopt;
    if (!line) host.err("\n");
    if (!line || !is_yes(*line)) {
        host.err(msg(Msg::DownloadDeclined));
        return {{}, kExitError};
    }
    fetch::DownloadOptions options;
    options.progress = host.stderr_tty;
    const fetch::DownloadResult d = fetch::download(spec.url, dir, spec.filename, spec.size, spec.sha256, options);
    switch (d.status) {
        case fetch::DownloadResult::Status::Ok:
            host.err(msg(Msg::DownloadDone, {d.path}));
            return {d.path, -1};
        case fetch::DownloadResult::Status::Interrupted:
        case fetch::DownloadResult::Status::Cancelled:  // only with on_progress, which the CLI does not set
            host.err("\n");
            return {{}, kExitInterrupted};
        case fetch::DownloadResult::Status::Failed:
            break;
    }
    host.err(msg(Msg::DownloadFailed, {d.error}));
    return {{}, kExitError};
}

// Stands in when no segment needs the model: check_text never calls it then.
class NoEngine final : public infer::Engine {
public:
    infer::EngineResult correct(std::string_view) override {
        infer::EngineResult r;
        r.error = "load_failed";
        r.error_detail = "no engine";
        return r;
    }
};

bool debug_env() {
    const char* v = std::getenv("REVISA_DEBUG");
    return v != nullptr && std::string_view(v) == "1";
}

// The engine of check, serve and lsp: the injected one, the REVISA_FAKE_* one (test hooks) or the
// model (downloaded after asking, only when interactive). On failure engine is null and exit_code
// is set, with the message already written.
struct EnginePick {
    std::unique_ptr<infer::Engine> engine;
    std::string model_path;  // empty for an injected or fake engine
    int exit_code = -1;
};

EnginePick open_engine(const Args& args, const config::Config& cfg, const std::string& explicit_path,
                       const ModelSpec& spec, const Host& host, bool interactive) {
    EnginePick out;
    if (host.make_engine) out.engine = host.make_engine();
#if REVISA_TEST_HOOKS
    if (!out.engine) out.engine = infer::fake_engine_from_env();
#endif
    if (out.engine) return out;
    const ModelPick pick = pick_model(explicit_path, spec, !interactive, host);
    if (pick.exit_code >= 0) {
        out.exit_code = pick.exit_code;
        return out;
    }
    infer::LlamaOptions options;
    options.model_path = pick.path;
    options.threads = args.threads ? *args.threads : cfg.threads > 0 ? cfg.threads : infer::default_threads();
    options.speculative = !args.no_spec;
    options.debug = debug_env();
    out.model_path = pick.path;
    out.engine = infer::make_llama_engine(options);
    return out;
}

#if REVISA_TEST_HOOKS
// A non-negative number from the environment, or nullopt (unset or not a number).
std::optional<double> env_number(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return std::nullopt;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    if (end == v || *end != '\0' || !(d >= 0)) return std::nullopt;
    return d;
}
#endif

// The Spanish message of an engine error of check_text.
void report_engine_error(const check::Result& r, const Host& host) {
    if (r.error == "load_failed") host.err(msg(Msg::LoadFailed, {r.error_detail}));
    else if (r.error == "decode_failed") host.err(msg(Msg::DecodeFailed, {r.error_detail}));
    else host.err(msg(Msg::EngineError, {r.error, r.error_detail}));
}

// --- revisa check --------------------------------------------------------------------------

struct Input {
    std::optional<std::string> file;  // nullopt = stdin
    std::string text;
};

int run_check(const Args& args, const Host& host) {
    const config::ConfigResult cfg = config::load_config();
    if (!cfg.config) {
        host.err(msg(Msg::Config, {cfg.error}));
        return kExitError;
    }
    const std::string explicit_path = args.model ? *args.model : cfg.config->model;
    const ModelSpec spec = default_model_spec();
    const std::string name = model_name(explicit_path, spec);

    // Every input is read before anything is written.
    std::vector<Input> inputs;
    std::string error;
    if (args.files.empty()) {
        auto text = host.read_stdin ? host.read_stdin(error) : std::optional<std::string>{};
        if (!text) {
            host.err(msg(Msg::ReadStdin, {error}));
            return kExitError;
        }
        inputs.push_back({std::nullopt, std::move(*text)});
    }
    for (const std::string& file : args.files) {
        auto text = read_file(file, error);
        if (!text) {
            host.err(msg(Msg::ReadFile, {file, error}));
            return kExitError;
        }
        inputs.push_back({file, std::move(*text)});
    }

    // The engine (and the model, maybe downloaded) only when some segment goes to the model.
    bool need = false;
    for (const Input& in : inputs) need = need || check::needs_model(in.text);
    std::unique_ptr<infer::Engine> engine;
    if (need) {
        EnginePick pick = open_engine(args, *cfg.config, explicit_path, spec, host, !args.json);
        if (!pick.engine) return pick.exit_code;
        engine = std::move(pick.engine);
    }
    if (!engine) engine = std::make_unique<NoEngine>();

    const check::Options options = check_options(args, *cfg.config);
    bool changed = false;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const Input& in = inputs[i];
        const check::Result r = check::check_text(in.text, *engine, options);
        if (!r.error.empty()) {
            report_engine_error(r, host);
            return kExitError;
        }
        changed = changed || r.changed;
        if (args.json) {
            host.out(json_line(in.file, name, r));
            continue;
        }
        if (inputs.size() > 1) {
            if (i > 0) host.out("\n");
            host.out(msg(Msg::FileHeader, {*in.file}));
        }
        host.out(r.corrected);
    }
    return changed ? kExitChanged : kExitUnchanged;
}

// --- JSONL ----------------------------------------------------------------------------------

// The text field of a JSONL object; nullopt (and the reason in error) if missing or not a string.
std::optional<std::string> text_field(const nlohmann::json& j, const char* name, bool required, std::string& error) {
    const auto it = j.find(name);
    if (it == j.end() && !required) return std::string();
    if (it == j.end() || !it->is_string()) {
        error = msg(Msg::JsonlField, {name});
        return std::nullopt;
    }
    return it->get<std::string>();
}

// One JSONL line as an object; on failure, the reason in error.
nlohmann::json parse_object(std::string_view line, std::string& error) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(line);
    } catch (const nlohmann::json::exception& e) {
        error = msg(Msg::JsonlInvalid, {e.what()});
        return j;
    }
    if (!j.is_object()) error = msg(Msg::JsonlNotObject);
    return j;
}

// Runs f(object, out_line, error) on each non-blank line of stdin. Everything is written at the
// end, and only if every line is valid: a bad line is an error with its number and exit 2.
template <typename F>
int run_jsonl(const Host& host, F&& f) {
    std::string error;
    const auto input = host.read_stdin ? host.read_stdin(error) : std::optional<std::string>{};
    if (!input) {
        host.err(msg(Msg::ReadStdin, {error}));
        return kExitError;
    }
    std::string out;
    std::string_view rest = *input;
    for (std::size_t lineno = 1; !rest.empty(); ++lineno) {
        const auto nl = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        rest.remove_prefix(nl == std::string_view::npos ? rest.size() : nl + 1);
        if (infer::trim_like_python(line).empty()) continue;
        const nlohmann::json j = parse_object(line, error);
        nlohmann::ordered_json o;
        if (error.empty()) {
            if (const auto id = j.find("id"); id != j.end()) o["id"] = *id;
            f(j, o, error);
        }
        if (!error.empty()) {
            host.err(msg(Msg::JsonlLine, {std::to_string(lineno), error}));
            return kExitError;
        }
        out += dump_line(o);
    }
    host.out(out);
    return kExitUnchanged;
}

// check --jsonl [--replay]: one case per stdin line, one output line per case as soon as it is
// done (the previous lines are already out when a later line fails). The engine is made at the
// first case that needs the model, and only once.
int run_check_jsonl(const Args& args, const Host& host) {
    const config::ConfigResult cfg = config::load_config();
    if (!cfg.config) {
        host.err(msg(Msg::Config, {cfg.error}));
        return kExitError;
    }
    const std::string explicit_path = args.model ? *args.model : cfg.config->model;
    const ModelSpec spec = default_model_spec();
    const check::Options options = check_options(args, *cfg.config);
    std::unique_ptr<infer::Engine> engine;
    NoEngine no_engine;

    for (std::size_t lineno = 1;; ++lineno) {
        const std::optional<std::string> line = host.read_line ? host.read_line() : std::nullopt;
        if (!line) break;
        if (infer::trim_like_python(*line).empty()) continue;
        std::string error;
        const nlohmann::json j = parse_object(*line, error);
        std::optional<std::string> text, corrected;
        if (error.empty()) text = text_field(j, "text", true, error);
        if (text && args.replay) corrected = text_field(j, "corrected", true, error);
        if (!error.empty()) {
            host.err(msg(Msg::JsonlLine, {std::to_string(lineno), error}));
            return kExitError;
        }

        check::Result r;
        int dropped = 0;
        if (args.replay) {
            infer::ReplayEngine replay(*text, *corrected);
            r = check::check_text(*text, replay, options);
            dropped = replay.dropped();
        } else {
            if (!engine && check::needs_model(*text)) {
                EnginePick pick = open_engine(args, *cfg.config, explicit_path, spec, host, false);
                if (!pick.engine) return pick.exit_code;
                engine = std::move(pick.engine);
            }
            r = check::check_text(*text, engine ? *engine : static_cast<infer::Engine&>(no_engine), options);
        }
        if (!r.error.empty()) {
            report_engine_error(r, host);
            return kExitError;
        }
        nlohmann::ordered_json o;
        if (const auto id = j.find("id"); id != j.end()) o["id"] = *id;
        o["corrected"] = r.corrected;
        o["edits"] = edits_json(r.edits);
        o["segments_skipped"] = r.segments_skipped;
        o["changed"] = r.changed;
        if (args.replay) o["replay_dropped"] = dropped;
        if (args.timings) {
            nlohmann::ordered_json t = nlohmann::ordered_json::array();
            for (const check::SegmentTiming& s : r.timings)
                t.push_back({{"ms", std::round(s.ms * 10) / 10}, {"changed", s.changed}, {"tokens", s.tokens}});
            o["timings"] = std::move(t);
        }
        host.out(dump_line(o));
    }
    return kExitUnchanged;
}

// --- revisa diff and revisa classify ---------------------------------------------------------

// The edits of a whole pair, each with "out_of_catalog": guard 4 with the original as the segment.
nlohmann::ordered_json pair_edits_json(std::string_view original, std::string_view corrected) {
    nlohmann::ordered_json a = nlohmann::ordered_json::array();
    for (const check::Edit& e : check::diff_pair(original, corrected)) {
        guard::Item item;
        item.edit.start = e.byte_start;
        item.edit.end = e.byte_end;
        item.edit.from = e.from;
        item.edit.to = e.to;
        item.result.type = e.type;
        item.result.explanation = e.explanation;
        nlohmann::ordered_json j = edit_json(e);
        j["out_of_catalog"] = guard::out_of_catalog(item, original);
        a.push_back(std::move(j));
    }
    return a;
}

int run_diff(const Args& args, const Host& host) {
    if (args.jsonl) {
        return run_jsonl(host, [](const nlohmann::json& j, nlohmann::ordered_json& o, std::string& error) {
            const auto original = text_field(j, "original", true, error);
            const auto corrected = original ? text_field(j, "corrected", true, error) : std::nullopt;
            if (corrected) o["edits"] = pair_edits_json(*original, *corrected);
        });
    }
    const std::vector<check::Edit> edits = check::diff_pair(args.texts[0], args.texts[1]);
    if (args.json) {
        nlohmann::ordered_json j;
        j["edits"] = edits_json(edits);
        host.out(dump_line(j));
    } else if (edits.empty()) {
        host.out(msg(Msg::DiffNone));
    } else {
        for (const check::Edit& e : edits) {
            const std::string a = std::to_string(e.start), b = std::to_string(e.end);
            if (e.explanation)
                host.out(msg(Msg::EditLine, {a, b, e.from, e.to, catalog::type_id(e.type), *e.explanation}));
            else
                host.out(msg(Msg::EditLineOther, {a, b, e.from, e.to}));
        }
    }
    return edits.empty() ? kExitUnchanged : kExitChanged;
}

int run_classify(const Host& host) {
    return run_jsonl(host, [](const nlohmann::json& j, nlohmann::ordered_json& o, std::string& error) {
        const auto from = text_field(j, "from", true, error);
        const auto to = from ? text_field(j, "to", true, error) : std::nullopt;
        const auto left = to ? text_field(j, "left", false, error) : std::nullopt;
        const auto right = left ? text_field(j, "right", false, error) : std::nullopt;
        if (!right) return;
        const classify::Result r = classify::classify(*from, *to, {*left, *right});
        o["type"] = std::string(catalog::type_id(r.type));
        if (r.explanation) o["explanation"] = *r.explanation;
        else o["explanation"] = nullptr;
    });
}

// segment --jsonl: the segments of each text exactly as check sees them (check::model_segments).
int run_segment(const Host& host) {
    return run_jsonl(host, [](const nlohmann::json& j, nlohmann::ordered_json& o, std::string& error) {
        const auto text = text_field(j, "text", true, error);
        if (!text) return;
        nlohmann::ordered_json a = nlohmann::ordered_json::array();
        for (const check::ModelSegment& s : check::model_segments(*text)) {
            nlohmann::ordered_json seg;
            seg["text"] = s.text;
            seg["skip"] = check::skip_name(s.skip);
            a.push_back(std::move(seg));
        }
        o["segments"] = std::move(a);
    });
}

}  // namespace

ServiceEngine open_service_engine(const Args& args, const config::Config& cfg, const Host& host) {
    const std::string explicit_path = args.model ? *args.model : cfg.model;
    const ModelSpec spec = default_model_spec();
    EnginePick pick = open_engine(args, cfg, explicit_path, spec, host, false);
    ServiceEngine out;
    out.exit_code = pick.exit_code;
    out.engine = std::move(pick.engine);
    out.model_name = model_name(explicit_path, spec);
    out.model_id = pick.model_path.empty() ? out.model_name : pick.model_path;
    return out;
}

check::Options check_options(const Args& args, const config::Config& cfg) {
    check::Options options;
    options.disabled = cfg.disabled_types;
    options.guards = !args.no_guards;
#if REVISA_TEST_HOOKS
    if (const auto r = env_number("REVISA_GUARD_MAX_CHANGE_RATIO")) options.thresholds.max_change_ratio = *r;
    if (const auto n = env_number("REVISA_GUARD_MAX_OTRO")) options.thresholds.max_otro = static_cast<int>(*n);
#endif
    return options;
}

int run(const std::vector<std::string>& argv, const Host& host) {
    const Args args = parse_args(argv);
    using E = Args::Error;
    if (args.error != E::None) {
        switch (args.error) {
            case E::NoCommand: host.err(msg(Msg::NoCommand)); break;
            case E::UnknownCommand: host.err(msg(Msg::UnknownCommand, {args.bad})); break;
            case E::UnknownFlag: host.err(msg(Msg::UnknownFlag, {args.bad})); break;
            case E::MissingValue: host.err(msg(Msg::MissingValue, {args.bad})); break;
            case E::BadThreads:
                host.err(msg(Msg::BadThreads, {args.bad, std::to_string(config::kMaxThreads)}));
                break;
            case E::PrintPromptExtra: host.err(msg(Msg::PrintPromptExtra, {args.bad})); break;
            case E::FlagNotForCommand: host.err(msg(Msg::FlagNotForCommand, {args.bad, args.command_word})); break;
            case E::DiffArgs: host.err(msg(Msg::DiffArgs)); break;
            case E::ClassifyArgs: host.err(msg(Msg::ClassifyArgs)); break;
            case E::FlagNeeds: host.err(msg(Msg::FlagNeeds, {args.bad, args.needs})); break;
            case E::CheckJsonlFiles: host.err(msg(Msg::CheckJsonlFiles)); break;
            case E::SegmentArgs: host.err(msg(Msg::SegmentArgs)); break;
            case E::BadPort: host.err(msg(Msg::BadPort, {args.bad})); break;
            case E::NoOperands: host.err(msg(Msg::NoOperands, {args.command_word, args.bad})); break;
            case E::TokenArgs: host.err(msg(Msg::TokenArgs)); break;
            case E::None: break;
        }
        host.err(msg(Msg::UsageHint));
        return kExitError;
    }
    switch (args.command) {
        case Args::Command::Help:
            host.out(msg(Msg::Help));
            return kExitUnchanged;
        case Args::Command::Version:
            host.out(msg(Msg::Version, {REVISA_VERSION, std::to_string(infer::kContractVersion)}));
            return kExitUnchanged;
        case Args::Command::PrintPrompt:
            host.out(infer::render_qwen35(args.prompt_text));
            return kExitUnchanged;
        case Args::Command::Check:
            return args.jsonl ? run_check_jsonl(args, host) : run_check(args, host);
        case Args::Command::Diff:
            return run_diff(args, host);
        case Args::Command::Classify:
            return run_classify(host);
        case Args::Command::Segment:
            return run_segment(host);
        case Args::Command::Serve:
            return run_serve(args, host);
        case Args::Command::Lsp:
            return run_lsp(args, host);
        case Args::Command::Token:
            return run_token(args, host);
        case Args::Command::None:
            break;
    }
    return kExitError;  // unreachable: parse_args sets an error or a command
}

int run(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);  // UTF-8 on Windows too (manifest)
#ifdef _WIN32
    // Bytes in and out unchanged (no CRLF translation, no 0x1A end of file): --json offsets and the
    // corrected text are the same as on Linux. The console shows and reads UTF-8.
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
    _setmode(2, _O_BINARY);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#else
    std::signal(SIGINT, on_sigint);
#endif

    Host host;
    host.out = [](std::string_view t) { write_fd(1, t); };
    host.err = [](std::string_view t) { write_fd(2, t); };
    host.read_line = read_stdin_line;
    host.read_stdin = [](std::string& error) { return read_all(0, error); };
    host.stdin_tty = sys_isatty(0);
    host.stderr_tty = sys_isatty(2);
    return run(args, host);
}

}  // namespace revisa::cli
