// core/catalog: embedded data files, their format, the type table and the explanation templates.
#include "doctest.h"

#include <filesystem>
#include <set>
#include <string>

#include "core/catalog/catalog.hpp"
#include "jsonl.hpp"

using namespace revisa;
namespace fs = std::filesystem;

TEST_CASE("every data file of src/core/catalog is embedded byte for byte") {
    const fs::path dir = fs::path(REVISA_TESTS_DIR).parent_path() / "src/core/catalog";
    int n = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = fs::relative(e.path(), dir).generic_string();
        const std::string ext = e.path().extension().string();
        const bool data = rel.starts_with("hunspell/") ? (rel == "hunspell/es_ES.aff" || rel == "hunspell/es_ES.dic")
                                                       : (ext == ".tsv" || ext == ".txt");
        if (!data) continue;
        const auto embedded = catalog::data_file(rel);
        REQUIRE_MESSAGE(embedded.has_value(), rel);
        CHECK_MESSAGE(*embedded == test::read_text_file(e.path()), rel);
        ++n;
    }
    CHECK(n == static_cast<int>(catalog::data_files().size()));
    CHECK_FALSE(catalog::data_file("no/such.tsv").has_value());
}

TEST_CASE("data files are valid UTF-8 with no stray spaces around the fields") {
    for (const std::string_view path : catalog::data_files()) {
        if (path.starts_with("hunspell/")) continue;
        for (const catalog::Row& r : catalog::rows(path)) {
            const std::string where = std::string(path) + ":" + std::to_string(r.line);
            for (const std::string_view col : r.cols) {
                CHECK_MESSAGE(nlohmann::json(std::string(col)).dump(-1, ' ', false,
                                                                    nlohmann::json::error_handler_t::strict)
                                  .size() > 0,
                              where);
                if (!col.empty()) {
                    CHECK_MESSAGE(col.front() != ' ', where << ": leading space");
                    CHECK_MESSAGE(col.back() != ' ', where << ": trailing space");
                }
            }
        }
    }
}

TEST_CASE("type ids round-trip and tipos.tsv lists the 31 types in catalog order") {
    for (std::size_t i = 0; i < catalog::kTypeCount; ++i) {
        const auto t = static_cast<catalog::Type>(i);
        CHECK(catalog::type_from_id(catalog::type_id(t)) == t);
    }
    CHECK_FALSE(catalog::type_from_id("NOPE").has_value());
    const auto& rows = catalog::rows("tipos.tsv");
    REQUIRE(rows.size() == catalog::kTypeCount - 1);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        REQUIRE(rows[i].cols.size() == 2);
        CHECK(rows[i].cols[0] == catalog::kTypeIds[i]);
        CHECK(rows[i].cols[1] == "sí");
    }
}

TEST_CASE("explanation templates: known types, known placeholders, no duplicates, «» quotes") {
    const std::set<std::string> known = {"de", "a", "letra", "regla", "verbo", "expresión", "con/sin"};
    std::set<std::pair<std::string, std::string>> seen;
    for (const std::string_view path : catalog::data_files()) {
        if (!path.starts_with("explicaciones/")) continue;
        for (const catalog::Row& r : catalog::rows(path)) {
            const std::string where = std::string(path) + ":" + std::to_string(r.line);
            REQUIRE_MESSAGE(r.cols.size() == 3, where);
            CHECK_MESSAGE((r.cols[0] == "REGLA" || catalog::type_from_id(r.cols[0]).has_value()), where);
            CHECK_MESSAGE(seen.emplace(std::string(r.cols[0]), std::string(r.cols[1])).second, where << ": duplicate");
            const std::string_view t = r.cols[2];
            CHECK_MESSAGE(t.find('"') == std::string_view::npos, where << ": use «» quotes");
            for (std::size_t i = t.find('{'); i != std::string_view::npos; i = t.find('{', i + 1)) {
                const std::size_t close = t.find('}', i);
                REQUIRE_MESSAGE(close != std::string_view::npos, where);
                CHECK_MESSAGE(known.count(std::string(t.substr(i + 1, close - i - 1))) == 1, where);
            }
        }
    }
    // Every type except OTRO has a default explanation or a pair list with its own explanations.
    for (std::size_t i = 0; i + 1 < catalog::kTypeCount; ++i) {
        const auto t = static_cast<catalog::Type>(i);
        const std::string id(catalog::type_id(t));
        bool any = catalog::explanation_template(t).has_value() ||
                   catalog::data_file("pares/" + id + ".tsv").has_value();
        for (const std::string_view path : catalog::data_files()) {
            if (!path.starts_with("explicaciones/")) continue;
            for (const catalog::Row& r : catalog::rows(path)) any = any || (r.cols.size() == 3 && r.cols[0] == id);
        }
        CHECK_MESSAGE(any, id);
    }
}

TEST_CASE("fill") {
    CHECK(catalog::fill("«{a}» lleva tilde: {regla}.", {{"a", "estás"}, {"regla", "es aguda"}}) ==
          "«estás» lleva tilde: es aguda.");
    CHECK(catalog::fill("{x} {a}", {{"a", "b"}}) == "{x} b");
    CHECK(catalog::fill("sin cierre {a", {{"a", "b"}}) == "sin cierre {a");
    CHECK(catalog::explanation_template(catalog::Type::Errata) == "Errata: «{de}» → «{a}».");
}
