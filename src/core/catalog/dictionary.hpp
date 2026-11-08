#pragma once
// Spelling dictionary of ERRATA: LibreOffice's RLA-ES Hunspell es_ES (hunspell/es_ES.{aff,dic},
// pinned commit and SHA-256 in the header of tests/dictionary/oraculo.tsv), embedded and read as
// data. The lookup is our own: it implements the subset of the Hunspell format that es_ES uses
// (SET UTF-8, FLAG UTF-8, PFX/SFX with cross product, one level of suffix continuation classes).
// An oracle labelled by the hunspell CLI (tests/dictionary/) checks it.

#include <string_view>

namespace revisa::catalog {

// true if word is a correct spelling for Hunspell es_ES. Capitalization follows Hunspell: a word
// with an initial capital or all in capitals is also accepted if its lowercase form is (an
// all-lowercase word does not match an entry that is capitalized in the dictionary, such as a
// proper noun). A word with spaces or no letters is never in the dictionary.
// The dictionary is parsed on the first call (thread-safe).
bool in_dictionary(std::string_view word);

}  // namespace revisa::catalog
