#pragma once
// Case files in jsonl (tests/<module>/*.jsonl): one JSON object per line; blank lines and lines
// starting with '#' are comments. Every case carries "where" (file:line) for failure messages.
// Deterministic code must pass 100 % of its cases.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"

namespace revisa::test {

struct JsonCase {
    std::string where;  // "dir/file.jsonl:line"
    nlohmann::json j;
};

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Cases of one file. An unreadable file or an invalid line fails the current test.
inline std::vector<JsonCase> load_jsonl(const std::filesystem::path& path) {
    std::vector<JsonCase> out;
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), ("missing " + path.string()));
    const std::string name = path.parent_path().filename().string() + "/" + path.filename().string();
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line[b] == '#') continue;
        JsonCase c;
        c.where = name + ":" + std::to_string(lineno);
        try {
            c.j = nlohmann::json::parse(line);
        } catch (const nlohmann::json::exception& e) {
            FAIL_CHECK(c.where << ": invalid JSON: " << e.what());
            continue;
        }
        out.push_back(std::move(c));
    }
    return out;
}

// Cases of every *.jsonl in tests/<dir>/, files in name order.
inline std::vector<JsonCase> load_jsonl_dir(const std::string& dir) {
    const std::filesystem::path base = std::filesystem::path(REVISA_TESTS_DIR) / dir;
    std::vector<std::filesystem::path> files;
    REQUIRE_MESSAGE(std::filesystem::is_directory(base), ("missing " + base.string()));
    for (const auto& e : std::filesystem::directory_iterator(base))
        if (e.path().extension() == ".jsonl") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::vector<JsonCase> out;
    for (const auto& p : files) {
        auto cases = load_jsonl(p);
        out.insert(out.end(), std::make_move_iterator(cases.begin()), std::make_move_iterator(cases.end()));
    }
    return out;
}

}  // namespace revisa::test
