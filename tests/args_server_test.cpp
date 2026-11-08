// parse_args: serve, lsp, token and the flags --no-spec, --timings and --port.
#include "doctest.h"

#include <string>
#include <vector>

#include "cli/args.hpp"

using revisa::cli::Args;
using revisa::cli::parse_args;
using C = Args::Command;
using E = Args::Error;

TEST_CASE("args: serve and lsp") {
    Args a = parse_args({"serve"});
    CHECK(a.error == E::None);
    CHECK(a.command == C::Serve);
    CHECK_FALSE(a.port);
    CHECK_FALSE(a.no_spec);

    a = parse_args({"serve", "--port", "8080", "--no-spec", "--model", "/m.gguf", "--threads=4"});
    CHECK(a.error == E::None);
    CHECK(a.command == C::Serve);
    CHECK(a.port == 8080);
    CHECK(a.no_spec);
    CHECK(a.model == "/m.gguf");
    CHECK(a.threads == 4);
    CHECK(parse_args({"serve", "--port=1"}).port == 1);
    CHECK(parse_args({"serve", "--port=65535"}).port == 65535);

    a = parse_args({"lsp", "--no-spec", "--model=/m.gguf"});
    CHECK(a.error == E::None);
    CHECK(a.command == C::Lsp);
    CHECK(a.no_spec);
    CHECK(a.model == "/m.gguf");
}

TEST_CASE("args: serve and lsp take no operands") {
    for (const char* cmd : {"serve", "lsp"}) {
        CAPTURE(cmd);
        const Args a = parse_args({cmd, "fichero.txt", "otro"});
        CHECK(a.error == E::NoOperands);
        CHECK(a.bad == "fichero.txt");
        CHECK(a.command == C::None);
    }
}

TEST_CASE("args: --port") {
    for (const char* bad : {"0", "65536", "99999", "-1", "80a", "123456", "+80"}) {
        CAPTURE(bad);
        const Args a = parse_args({"serve", "--port", bad});
        CHECK(a.error == E::BadPort);
        CHECK(a.bad == bad);
    }
    CHECK(parse_args({"serve", "--port"}).error == E::MissingValue);
    CHECK(parse_args({"serve", "--port="}).error == E::MissingValue);
    for (const char* cmd : {"check", "lsp", "diff", "token"}) {
        CAPTURE(cmd);
        const Args a = parse_args({cmd, "--port", "8080"});
        CHECK(a.error == E::FlagNotForCommand);
        CHECK(a.bad == "--port");
    }
}

TEST_CASE("args: serve and lsp reject the check-only flags") {
    for (const char* cmd : {"serve", "lsp"}) {
        for (const char* flag : {"--json", "--jsonl", "--replay", "--no-guards", "--timings"}) {
            CAPTURE(cmd);
            CAPTURE(flag);
            const Args a = parse_args({cmd, flag});
            CHECK(a.error == E::FlagNotForCommand);
            CHECK(a.bad == flag);
            CHECK(a.command_word == cmd);
        }
    }
}

TEST_CASE("args: token") {
    Args a = parse_args({"token", "new", "portatil"});
    CHECK(a.error == E::None);
    CHECK(a.command == C::Token);
    CHECK(a.texts == std::vector<std::string>{"new", "portatil"});

    a = parse_args({"token", "list"});
    CHECK(a.error == E::None);
    CHECK(a.texts == std::vector<std::string>{"list"});

    a = parse_args({"token", "revoke", "portatil"});
    CHECK(a.error == E::None);
    CHECK(a.texts == std::vector<std::string>{"revoke", "portatil"});

    const std::vector<std::vector<std::string>> bad = {
        {"token"},
        {"token", "new"},
        {"token", "revoke"},
        {"token", "list", "x"},
        {"token", "new", "a", "b"},
        {"token", "borra", "x"},
        {"token", "x"},
    };
    for (const auto& args : bad) {
        CAPTURE(args.size());
        const Args t = parse_args(args);
        CHECK(t.error == E::TokenArgs);
        CHECK(t.command == C::None);
    }
    for (const char* flag : {"--jsonl", "--json", "--no-spec", "--timings", "--no-guards"}) {
        CAPTURE(flag);
        const Args t = parse_args({"token", "list", flag});
        CHECK(t.error == E::FlagNotForCommand);
        CHECK(t.bad == flag);
    }
    CHECK(parse_args({"token", "list", "--model", "/m.gguf"}).error == E::FlagNotForCommand);
}

TEST_CASE("args: --no-spec") {
    Args a = parse_args({"check", "--no-spec"});
    CHECK(a.error == E::None);
    CHECK(a.no_spec);
    a = parse_args({"check", "--jsonl", "--no-spec"});
    CHECK(a.error == E::None);
    CHECK(a.no_spec);
    CHECK(parse_args({"check", "--json", "--no-spec", "a.txt"}).error == E::None);

    a = parse_args({"check", "--jsonl", "--replay", "--no-spec"});
    CHECK(a.error == E::FlagNotForCommand);
    CHECK(a.bad == "--no-spec");
    CHECK(a.command_word == "check --replay");
    for (const char* cmd : {"diff", "classify", "segment"}) {
        CAPTURE(cmd);
        a = parse_args({cmd, "--jsonl", "--no-spec"});
        CHECK(a.error == E::FlagNotForCommand);
        CHECK(a.bad == "--no-spec");
    }
}

TEST_CASE("args: --timings") {
    Args a = parse_args({"check", "--jsonl", "--timings"});
    CHECK(a.error == E::None);
    CHECK(a.command == C::Check);
    CHECK(a.timings);

    a = parse_args({"check", "--timings"});
    CHECK(a.error == E::FlagNeeds);
    CHECK(a.bad == "--timings");
    CHECK_FALSE(a.needs.empty());
    a = parse_args({"check", "--json", "--timings"});
    CHECK(a.error == E::FlagNeeds);
    CHECK(a.bad == "--timings");

    a = parse_args({"check", "--jsonl", "--replay", "--timings"});
    CHECK(a.error == E::FlagNotForCommand);
    CHECK(a.bad == "--timings");
    CHECK(a.command_word == "check --replay");

    for (const char* cmd : {"diff", "classify", "segment"}) {
        CAPTURE(cmd);
        a = parse_args({cmd, "--jsonl", "--timings"});
        CHECK(a.error == E::FlagNotForCommand);
        CHECK(a.bad == "--timings");
    }
}
