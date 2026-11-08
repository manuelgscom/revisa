#include "cli/args.hpp"

#include <initializer_list>
#include <string_view>
#include <utility>

#include "core/config/config.hpp"

namespace revisa::cli {

namespace {

// Subcommands, by the word that selects them.
struct CommandName {
    std::string_view word;
    Args::Command command;
};
constexpr CommandName kCommands[] = {
    {"check", Args::Command::Check},
    {"diff", Args::Command::Diff},
    {"classify", Args::Command::Classify},
    {"segment", Args::Command::Segment},
    {"serve", Args::Command::Serve},
    {"lsp", Args::Command::Lsp},
    {"token", Args::Command::Token},
};

// Flags that take a value.
constexpr std::string_view kValueFlags[] = {"--model", "--threads", "--port", "--print-prompt"};

// "1".."65535", digits only.
std::optional<int> parse_port(const std::string& value) {
    if (value.empty() || value.size() > 5) return std::nullopt;
    int n = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    if (n < 1 || n > 65535) return std::nullopt;
    return n;
}

// "1".."kMaxThreads", digits only.
std::optional<int> parse_threads(const std::string& value) {
    if (value.empty() || value.size() > 3) return std::nullopt;
    int n = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    if (n < 1 || n > config::kMaxThreads) return std::nullopt;
    return n;
}

// The first of the seen flags that is one of names, or "".
std::string first_flag(const std::vector<std::string>& seen, std::initializer_list<std::string_view> names) {
    for (const std::string& flag : seen)
        for (const std::string_view n : names)
            if (flag == n) return flag;
    return {};
}

// The flags and operands of the subcommand in out.command.
void check_command(Args& out, const std::vector<std::string>& seen, std::vector<std::string> operands) {
    using E = Args::Error;
    using C = Args::Command;
    switch (out.command) {
        case C::Check: out.command_word = "check"; break;
        case C::Diff: out.command_word = "diff"; break;
        case C::Classify: out.command_word = "classify"; break;
        case C::Segment: out.command_word = "segment"; break;
        case C::Serve: out.command_word = "serve"; break;
        case C::Lsp: out.command_word = "lsp"; break;
        default: out.command_word = "token"; break;
    }
    std::string bad;
    switch (out.command) {
        case C::Check:
            if (out.jsonl && out.json) {
                bad = "--json";
                out.command_word = "check --jsonl";
            } else if (out.replay && out.jsonl) {
                bad = first_flag(seen, {"--model", "--threads", "--no-spec", "--timings"});
                if (!bad.empty()) out.command_word = "check --replay";
            }
            if (bad.empty()) bad = first_flag(seen, {"--port"});
            break;
        case C::Diff:
            bad = first_flag(seen, {"--model", "--threads", "--replay", "--no-guards", "--no-spec", "--timings", "--port"});
            break;
        case C::Serve:
            bad = first_flag(seen, {"--json", "--jsonl", "--replay", "--no-guards", "--timings"});
            break;
        case C::Lsp:
            bad = first_flag(seen, {"--json", "--jsonl", "--replay", "--no-guards", "--timings", "--port"});
            break;
        default:
            bad = first_flag(seen, {"--model", "--threads", "--json", "--replay", "--no-guards", "--no-spec",
                                    "--timings", "--port"});
            if (bad.empty() && out.command == C::Token) bad = first_flag(seen, {"--jsonl"});
            break;
    }
    if (bad.empty() && out.command == C::Diff && out.json && out.jsonl) bad = "--json";
    if (!bad.empty()) {
        out.error = E::FlagNotForCommand;
        out.bad = bad;
        return;
    }
    switch (out.command) {
        case C::Check:
            if (out.replay && !out.jsonl) {
                out.error = E::FlagNeeds;
                out.bad = "--replay";
                out.needs = "«revisa check --jsonl»";
            } else if (out.timings && !out.jsonl) {
                out.error = E::FlagNeeds;
                out.bad = "--timings";
                out.needs = "«revisa check --jsonl»";
            } else if (out.no_guards && !out.json && !out.jsonl) {
                out.error = E::FlagNeeds;
                out.bad = "--no-guards";
                out.needs = "«revisa check --json» o «revisa check --jsonl»";
            } else if (out.jsonl && !operands.empty()) {
                out.error = E::CheckJsonlFiles;
            } else {
                out.files = std::move(operands);
            }
            break;
        case C::Diff:
            if (operands.size() != (out.jsonl ? 0U : 2U)) out.error = E::DiffArgs;
            else out.texts = std::move(operands);
            break;
        case C::Serve:
        case C::Lsp:
            if (!operands.empty()) {
                out.error = E::NoOperands;
                out.bad = operands.front();
            }
            break;
        case C::Token: {
            const bool ok = (operands.size() == 1 && operands[0] == "list") ||
                            (operands.size() == 2 && (operands[0] == "new" || operands[0] == "revoke"));
            if (ok) out.texts = std::move(operands);
            else out.error = E::TokenArgs;
            break;
        }
        default:
            if (!out.jsonl || !operands.empty())
                out.error = out.command == C::Classify ? E::ClassifyArgs : E::SegmentArgs;
            break;
    }
}

}  // namespace

Args parse_args(const std::vector<std::string>& args) {
    Args out;
    bool help = false, version = false, flags_done = false;
    std::optional<std::string> print_prompt;
    std::string unknown, missing, bad_threads, bad_port;
    std::vector<std::string> words;
    std::vector<std::string> seen;  // flags given, in order (canonical names)

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (flags_done || a.size() < 2 || a[0] != '-') {
            words.push_back(a);
            continue;
        }
        if (a == "--") {
            flags_done = true;
        } else if (a == "-h" || a == "--help") {
            help = true;
        } else if (a == "--version") {
            version = true;
        } else if (a == "--json") {
            out.json = true;
            seen.push_back(a);
        } else if (a == "--jsonl") {
            out.jsonl = true;
            seen.push_back(a);
        } else if (a == "--replay") {
            out.replay = true;
            seen.push_back(a);
        } else if (a == "--no-guards") {
            out.no_guards = true;
            seen.push_back(a);
        } else if (a == "--no-spec") {
            out.no_spec = true;
            seen.push_back(a);
        } else if (a == "--timings") {
            out.timings = true;
            seen.push_back(a);
        } else {
            std::string flag = a;
            std::optional<std::string> value;
            if (const auto eq = flag.find('='); eq != std::string::npos && flag.starts_with("--")) {
                value = flag.substr(eq + 1);
                flag.resize(eq);
            }
            bool known = false;
            for (const std::string_view f : kValueFlags) known = known || flag == f;
            if (!known) {
                if (unknown.empty()) unknown = a;
                continue;
            }
            if (!value && i + 1 < args.size()) value = args[++i];
            // An empty text is a valid prompt; an empty path or number is not.
            if (!value || (value->empty() && flag != "--print-prompt")) {
                if (missing.empty()) missing = flag;
                continue;
            }
            seen.push_back(flag);
            if (flag == "--model") {
                out.model = *value;
            } else if (flag == "--threads") {
                out.threads = parse_threads(*value);
                if (!out.threads && bad_threads.empty()) bad_threads = *value;
            } else if (flag == "--port") {
                out.port = parse_port(*value);
                if (!out.port && bad_port.empty()) bad_port = *value;
            } else {
                print_prompt = *value;
            }
        }
    }

    using E = Args::Error;
    using C = Args::Command;
    if (!unknown.empty()) {
        out.error = E::UnknownFlag;
        out.bad = unknown;
    } else if (!missing.empty()) {
        out.error = E::MissingValue;
        out.bad = missing;
    } else if (!bad_threads.empty()) {
        out.error = E::BadThreads;
        out.bad = bad_threads;
    } else if (!bad_port.empty()) {
        out.error = E::BadPort;
        out.bad = bad_port;
    } else if (help) {
        out.command = C::Help;
    } else if (version) {
        out.command = C::Version;
    } else if (print_prompt) {
        if (!words.empty()) {
            out.error = E::PrintPromptExtra;
            out.bad = words.front();
        } else {
            out.command = C::PrintPrompt;
            out.prompt_text = *print_prompt;
        }
    } else if (words.empty()) {
        out.error = E::NoCommand;
    } else {
        for (const auto& [word, command] : kCommands)
            if (words.front() == word) out.command = command;
        if (out.command == C::None) {
            out.error = E::UnknownCommand;
            out.bad = words.front();
        } else {
            check_command(out, seen, std::vector<std::string>(words.begin() + 1, words.end()));
            if (out.error != E::None) out.command = C::None;
        }
    }
    return out;
}

}  // namespace revisa::cli
