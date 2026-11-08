// Admitted variants: variantes_admitidas.tsv.
#include <string>
#include <string_view>

#include "core/catalog/catalog.hpp"
#include "core/text/text.hpp"

namespace revisa::catalog {

namespace {

// s without leading and trailing whitespace (text::is_space).
std::string_view trim_spaces(std::string_view s) {
    std::size_t b = 0;
    while (b < s.size()) {
        const text::Decoded d = text::decode(s, b);
        if (!text::is_space(d.cp)) break;
        b += d.len;
    }
    std::size_t e = s.size();
    while (e > b) {
        std::size_t start = e - 1;
        while (start > b && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
        if (!text::is_space(text::decode(s, start).cp)) break;
        e = start;
    }
    return s.substr(b, e - b);
}

// The pair is a row of the list, in either order (lowercase).
bool listed(std::string_view a, std::string_view b) {
    for (const Row& r : rows("variantes_admitidas.tsv")) {
        if (r.cols.size() < 2) continue;
        if ((r.cols[0] == a && r.cols[1] == b) || (r.cols[0] == b && r.cols[1] == a)) return true;
    }
    return false;
}

// «cardíaca» -> «cardíaco»: the feminine of the -íaco/-iaco adjectives of the list.
std::string masculine_iaco(std::string s) {
    if (s.ends_with("íaca") || s.ends_with("iaca")) s.back() = 'o';
    return s;
}

}  // namespace

bool admitted_variant(std::string_view from, std::string_view to) {
    const std::string a = text::lower(trim_spaces(from));
    const std::string b = text::lower(trim_spaces(to));
    if (a.empty() || b.empty() || a == b) return false;
    if (listed(a, b)) return true;
    // The same plural on both sides: vídeos/videos, chóferes/choferes, cardíacas/cardiacas.
    for (const std::string_view plural : {std::string_view{}, std::string_view{"s"}, std::string_view{"es"}}) {
        if (!a.ends_with(plural) || !b.ends_with(plural)) continue;
        const std::string sa = masculine_iaco(a.substr(0, a.size() - plural.size()));
        const std::string sb = masculine_iaco(b.substr(0, b.size() - plural.size()));
        if (!sa.empty() && !sb.empty() && listed(sa, sb)) return true;
    }
    return false;
}

}  // namespace revisa::catalog
