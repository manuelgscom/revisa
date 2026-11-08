#include "core/catalog/catalog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

namespace revisa::catalog {

namespace detail {
// Generated at build time (CMakeLists.txt + cmake/embed.cmake).
struct EmbeddedFile {
    std::string_view path;
    std::string_view data;
};
extern const EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;
}  // namespace detail

std::optional<Type> type_from_id(std::string_view id) {
    for (std::size_t i = 0; i < kTypeCount; ++i)
        if (kTypeIds[i] == id) return static_cast<Type>(i);
    return std::nullopt;
}

std::optional<std::string_view> data_file(std::string_view path) {
    for (std::size_t i = 0; i < detail::kEmbeddedFileCount; ++i)
        if (detail::kEmbeddedFiles[i].path == path) return detail::kEmbeddedFiles[i].data;
    return std::nullopt;
}

std::vector<std::string_view> data_files() {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < detail::kEmbeddedFileCount; ++i) out.push_back(detail::kEmbeddedFiles[i].path);
    std::sort(out.begin(), out.end());
    return out;
}

namespace {

std::vector<Row> parse(std::string_view data) {
    std::vector<Row> out;
    std::size_t line = 0;
    while (!data.empty()) {
        ++line;
        const std::size_t nl = data.find('\n');
        std::string_view l = data.substr(0, nl);
        data = nl == std::string_view::npos ? std::string_view{} : data.substr(nl + 1);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        if (l.empty() || l.front() == '#') continue;
        Row row;
        row.line = line;
        for (;;) {
            const std::size_t tab = l.find('\t');
            row.cols.push_back(l.substr(0, tab));
            if (tab == std::string_view::npos) break;
            l.remove_prefix(tab + 1);
        }
        out.push_back(std::move(row));
    }
    return out;
}

}  // namespace

const std::vector<Row>& rows(std::string_view path) {
    static std::mutex mu;
    static std::map<std::string, std::vector<Row>, std::less<>> cache;
    const std::lock_guard<std::mutex> lock(mu);
    if (const auto it = cache.find(path); it != cache.end()) return it->second;
    const auto data = data_file(path);
    if (!data) {
        std::fprintf(stderr, "revisa: catalog data file not embedded: %.*s\n", static_cast<int>(path.size()),
                     path.data());
        std::abort();
    }
    return cache.emplace(std::string(path), parse(*data)).first->second;
}

std::optional<std::string_view> explanation_template(Type type, std::string_view variant) {
    const std::string_view id = type_id(type);
    for (const std::string_view file : data_files()) {
        if (!file.starts_with("explicaciones/")) continue;
        for (const Row& r : rows(file)) {
            if (r.cols.size() < 3) continue;
            if (r.cols[0] == id && r.cols[1] == variant) return r.cols[2];
        }
    }
    return std::nullopt;
}

std::string fill(std::string_view tmpl, std::initializer_list<std::pair<std::string_view, std::string_view>> vars) {
    std::string out;
    out.reserve(tmpl.size() + 32);
    std::size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] == '{') {
            const std::size_t close = tmpl.find('}', i + 1);
            if (close != std::string_view::npos) {
                const std::string_view name = tmpl.substr(i + 1, close - i - 1);
                bool replaced = false;
                for (const auto& [k, v] : vars) {
                    if (k == name) {
                        out += v;
                        replaced = true;
                        break;
                    }
                }
                if (replaced) {
                    i = close + 1;
                    continue;
                }
            }
        }
        out += tmpl[i++];
    }
    return out;
}

}  // namespace revisa::catalog
