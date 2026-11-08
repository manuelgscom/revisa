#pragma once
// Orthographic syllabification of one Spanish word (Ortografía 2010, ch. I and II), used by the
// classifier to compute the {regla} of TILDE_FALTA and TILDE_SOBRA.
//
// It follows the ORTHOGRAPHIC convention, the one the accent rules use:
//   - diphthong: open + unstressed closed vowel (or the reverse), or two different closed vowels
//     (ui, iu, even with a written accent: «cuídate», «destruí»);
//   - hiatus: two open vowels, two equal vowels, or a closed vowel with a written accent next to
//     an open one («país», «búho»);
//   - triphthong: closed + open + closed («buey», «averiguáis»);
//   - an «h» between vowels does not prevent the diphthong («prohibir» = prohi-bir);
//   - in «que», «qui», «gue», «gui» the «u» is silent; «ü» is a vowel («pingüino»);
//   - a final «y» after a vowel is part of the nucleus («hoy», «virrey»), but counts as a
//     consonant for the accent rules («virrey» is aguda without a tilde);
//   - inseparable onsets: pl pr bl br cl cr dr fl fr gl gr tr kl kr; «tl» is split (at-le-ta,
//     the usage in Spain); ch, ll, rr are single consonants.
// So «guion», «fie», «truhan», «riais» and «hui» are monosyllables.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace revisa::classify {

enum class Stress { Monosyllable, Aguda, Llana, Esdrujula, Sobresdrujula };

// The last letters of the word, as the accent rules see them.
enum class Ending {
    Vowel,      // a e i o u (with or without accent)
    N,          // «-n» after a vowel
    S,          // «-s» after a vowel
    Y,          // «-y» (a consonant for the accent rules)
    Consonant,  // any other single consonant after a vowel («reloj», «árbol»)
    Cluster,    // two or more consonants («bíceps», «robots», «récords»)
};

struct Syllables {
    std::vector<std::string> parts;  // lowercase syllables, in order
    std::size_t stressed = 0;        // index of the stressed syllable in parts
    Stress stress = Stress::Monosyllable;
    Ending ending = Ending::Vowel;
    bool tilde = false;         // the word has a written accent (on the stressed vowel)
    bool hiatus_tilde = false;  // that accent is on í/ú next to an open vowel (marks a hiatus)
    int vowels = 0;             // vowel letters in the word (a final «y» in the nucleus counts)
};

// nullopt if the word is empty or has anything but Spanish letters (a-z, ñ, á é í ó ú, ü, in
// any case).
std::optional<Syllables> syllabify(std::string_view word);

// "monosilaba", "aguda", "llana", "esdrujula", "sobresdrujula".
std::string_view stress_name(Stress s);

// "ca-mión": the syllables joined with '-'.
std::string join_syllables(const Syllables& s);

}  // namespace revisa::classify
