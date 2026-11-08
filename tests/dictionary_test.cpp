// core/catalog dictionary: our Hunspell es_ES lookup against the hunspell CLI (oracle) and by hand.
#include "doctest.h"

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

#include "core/catalog/dictionary.hpp"

using revisa::catalog::in_dictionary;

// First in this file: with -tc='dictionary*' this call parses the dictionary. If another test
// already loaded it, the measured time is just one lookup.
TEST_CASE("dictionary: load time") {
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(in_dictionary("casa"));
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("dictionary load + first lookup: " << ms << " ms");
    CHECK(ms < 500);
}

TEST_CASE("dictionary: agrees with the hunspell CLI on tests/dictionary/oraculo.tsv") {
    const std::string path = std::string(REVISA_TESTS_DIR) + "/dictionary/oraculo.tsv";
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), ("missing " + path));
    std::string line;
    int n = 0, accepted = 0;
    std::vector<std::string> diffs;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto tab = line.rfind('\t');
        REQUIRE_MESSAGE(tab != std::string::npos, ("bad line: " + line));
        const std::string word = line.substr(0, tab);
        const std::string label = line.substr(tab + 1);
        REQUIRE_MESSAGE((label == "1" || label == "0"), ("bad label: " + line));
        const bool want = label == "1";
        ++n;
        accepted += want ? 1 : 0;
        if (in_dictionary(word) != want) diffs.push_back(word + " (hunspell: " + label + ")");
    }
    std::string first;
    for (std::size_t i = 0; i < diffs.size() && i < 20; ++i) first += "\n  " + diffs[i];
    MESSAGE(n << " words, " << accepted << " accepted by hunspell, " << diffs.size() << " disagree" << first);
    CHECK(n >= 30000);
    CHECK(diffs.empty());
}

TEST_CASE("dictionary: hand cases") {
    CHECK(in_dictionary("casa"));
    CHECK(in_dictionary("Casa"));
    CHECK(in_dictionary("CASA"));
    CHECK(in_dictionary("casas"));
    CHECK_FALSE(in_dictionary("cAsa"));    // mixed case: only as written
    CHECK_FALSE(in_dictionary("madrid"));  // proper noun in lowercase
    CHECK(in_dictionary("Madrid"));
    CHECK(in_dictionary("MADRID"));
    CHECK(in_dictionary("dígamelo"));  // two-level suffix (enclitics)
    CHECK(in_dictionary("poniéndoselo"));
    CHECK(in_dictionary("tráemelo"));
    CHECK(in_dictionary("dímelo"));
    CHECK(in_dictionary("Dígamelo"));
    // RLA-ES v2.9 does not generate these forms: the hunspell CLI rejects them too.
    CHECK_FALSE(in_dictionary("cómpraselo"));
    CHECK_FALSE(in_dictionary("cómpralo"));
    CHECK(in_dictionary("ADN"));          // all-caps entry without flags
    CHECK_FALSE(in_dictionary("adn"));
    CHECK_FALSE(in_dictionary("Adn"));
    CHECK(in_dictionary("Antiinflamatorio"));  // prefix, capitalized
    CHECK_FALSE(in_dictionary("digamelo"));
    CHECK_FALSE(in_dictionary("cassa"));
    CHECK_FALSE(in_dictionary("hola mundo"));
    CHECK_FALSE(in_dictionary(""));
    CHECK_FALSE(in_dictionary("123"));
    CHECK_FALSE(in_dictionary("..."));
    CHECK_FALSE(in_dictionary(" "));
    CHECK_FALSE(in_dictionary("\xC3"));  // invalid UTF-8
}
