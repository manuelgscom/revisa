/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * Copyright (C) 2002-2022 Németh László
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Hunspell is based on MySpell which is Copyright (C) 2002 Kevin Hendricks.
 *
 * Contributor(s): David Einstein, Davide Prina, Giuseppe Modugno,
 * Gianluca Turconi, Simon Brouwer, Noll János, Bíró Árpád,
 * Goldman Eleonóra, Sarlós Tamás, Bencsáth Boldizsár, Halácsy Péter,
 * Dvornik László, Gefferth András, Nagy Viktor, Varga Dániel, Chris Halls,
 * Rene Engelhard, Bram Moolenaar, Dafydd Jones, Harri Pitkänen
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
/*
 * Copyright 2002 Kevin B. Hendricks, Stratford, Ontario, Canada
 * And Contributors.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All modifications to the source code must be clearly marked as
 *    such.  Binary redistributions based on modified source code
 *    must be clearly marked as modified versions in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY KEVIN B. HENDRICKS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * KEVIN B. HENDRICKS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * This file is a modified version: a C++20 port, for revisa, of the lookup path of Hunspell
 * 1.7.2 (see the comment below). It keeps the licence of the original code: MPL 1.1 / GPL 2.0 /
 * LGPL 2.1, and the MySpell notice above. The rest of revisa is MIT (THIRD_PARTY_NOTICES).
 */

// Spelling lookup over the embedded Hunspell es_ES dictionary (see dictionary.hpp).
//
// This is a port of the lookup path of Hunspell 1.7.2 (hunspell.cxx spell()/checkword(),
// affixmgr.cxx affix_check() and its prefix/suffix/two-level-suffix helpers, affentry.cxx
// checkword()/check_twosfx()/test_condition(), hashmgr.cxx load_tables()/add_word()), restricted
// to what es_ES.aff uses: SET UTF-8, FLAG UTF-8, PFX/SFX (cross product), one level of suffix
// continuation classes. TRY/REP/MAP only drive suggestions and are ignored. No compounding,
// FORBIDDENWORD, NEEDAFFIX, KEEPCASE, ICONV or IGNORE (es_ES.aff defines none of them).
//
// Quirks kept on purpose, because the hunspell CLI (the oracle, tests/dictionary/) has them:
//   - flags are UTF-16 code units of the UTF-8 flag string (FLAG UTF-8); a 4-byte UTF-8 character
//     (an emoji flag) becomes U+FFFD and ends the flag string;
//   - suffix conditions are byte-reversed and matched backwards byte by byte (test_condition);
//   - a dictionary word that is all capitals with flags, or mixed case, gets a hidden
//     "Initcap" homonym usable only for ALL-CAPS input (ONLYUPCASEFLAG);
//   - trailing periods are an abbreviation; "-" splits a word (Hunspell's default BREAK).

#include "core/catalog/dictionary.hpp"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/text/text.hpp"

namespace revisa::catalog {
namespace {

using Flag = std::uint16_t;
using U16 = std::vector<std::uint16_t>;

constexpr Flag kOnlyUpcase = 65511;      // Hunspell's ONLYUPCASEFLAG
constexpr std::size_t kMaxWordUtf8 = 300;  // MAXWORDUTF8LEN

enum CapType { kNoCap, kInitCap, kAllCap, kHuhCap, kHuhInitCap };

[[noreturn]] void fail(const std::string& msg) {
    std::fprintf(stderr, "revisa: bad embedded Hunspell dictionary: %s\n", msg.c_str());
    std::abort();
}

// --- Hunspell's UTF-8 <-> UTF-16 (csutil.cxx u8_u16 / u16_u8) ---------------------------------

U16 u8_u16(std::string_view s) {
    U16 out;
    out.reserve(s.size());
    auto at = [&](std::size_t i) -> unsigned char { return i < s.size() ? s[i] : 0; };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char b = at(i);
        switch (b & 0xf0) {
            case 0x80:
            case 0x90:
            case 0xa0:
            case 0xb0:  // unexpected continuation byte
                out.push_back(0xfffd);
                break;
            case 0xc0:
            case 0xd0:
                if ((at(i + 1) & 0xc0) == 0x80) {
                    out.push_back(static_cast<std::uint16_t>(((b & 0x1f) << 6) | (at(i + 1) & 0x3f)));
                    ++i;
                } else {
                    out.push_back(0xfffd);
                }
                break;
            case 0xe0:
                if ((at(i + 1) & 0xc0) == 0x80) {
                    const unsigned hi = ((b & 0x0f) << 4) + ((at(i + 1) & 0x3f) >> 2);
                    ++i;
                    if ((at(i + 1) & 0xc0) == 0x80) {
                        const unsigned lo = (static_cast<unsigned>(at(i)) << 6) + (at(i + 1) & 0x3f);
                        out.push_back(static_cast<std::uint16_t>((hi << 8) | (lo & 0xff)));
                        ++i;
                    } else {
                        out.push_back(0xfffd);
                    }
                } else {
                    out.push_back(0xfffd);
                }
                break;
            default:
                if (b >= 0xf0) {  // 4-byte sequences are not supported: U+FFFD and stop
                    out.push_back(0xfffd);
                    return out;
                }
                out.push_back(b);  // ASCII
        }
    }
    return out;
}

std::string u16_u8(const U16& u) {
    std::string out;
    out.reserve(u.size() * 2);
    for (const std::uint16_t c : u) {
        if (c >= 0x800) {
            out.push_back(static_cast<char>(0xe0 + (c >> 12)));
            out.push_back(static_cast<char>(0x80 + ((c >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 + (c & 0x3f)));
        } else if (c >= 0x80) {
            out.push_back(static_cast<char>(0xc0 + (c >> 6)));
            out.push_back(static_cast<char>(0x80 + (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

std::uint16_t lower_unit(std::uint16_t c) {
    const char32_t l = text::to_lower(c);
    return l <= 0xffff ? static_cast<std::uint16_t>(l) : c;
}
std::uint16_t upper_unit(std::uint16_t c) {
    const char32_t u = text::to_upper(c);
    return u <= 0xffff ? static_cast<std::uint16_t>(u) : c;
}

void mkallsmall(U16& u) {
    for (auto& c : u) c = lower_unit(c);
}
void mkinitcap(U16& u) {
    if (!u.empty()) u[0] = upper_unit(u[0]);
}

CapType captype(const U16& w) {  // csutil.cxx get_captype_utf8
    std::size_t ncap = 0, nneutral = 0;
    for (const auto c : w) {
        const auto lw = lower_unit(c);
        if (c != lw) ++ncap;
        if (upper_unit(c) == lw) ++nneutral;
    }
    if (ncap == 0) return kNoCap;
    const bool firstcap = w[0] != lower_unit(w[0]);
    if (ncap == 1 && firstcap) return kInitCap;
    if (ncap == w.size() || ncap + nneutral == w.size()) return kAllCap;
    if (ncap > 1 && firstcap) return kHuhInitCap;
    return kHuhCap;
}

// --- Affix conditions (affixmgr.cxx parse_affix, affentry.cxx test_condition) -----------------

void reverse_condition(std::string& piece) {
    if (piece.empty()) return;
    const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(piece.size());
    auto next = [&](std::ptrdiff_t k) -> char { return k + 1 < n ? piece[k + 1] : '\0'; };
    auto set_next = [&](std::ptrdiff_t k, char c) {
        if (k + 1 < n) piece[k + 1] = c;
    };
    bool neg = false;
    for (std::ptrdiff_t k = n - 1; k >= 0; --k) {
        switch (piece[k]) {
            case '[':
                if (neg)
                    set_next(k, '[');
                else
                    piece[k] = ']';
                break;
            case ']':
                piece[k] = '[';
                if (neg) set_next(k, '^');
                neg = false;
                break;
            case '^':
                if (next(k) == ']')
                    neg = true;
                else if (neg)
                    set_next(k, piece[k]);
                break;
            default:
                if (neg) set_next(k, piece[k]);
        }
    }
}

int condlen(const std::string& s) {
    int l = 0;
    bool group = false;
    for (const char c : s) {
        if (c == '[') {
            group = true;
            ++l;
        } else if (c == ']') {
            group = false;
        } else if (!group && (!(c & 0x80) || ((c & 0xc0) == 0x80))) {
            ++l;
        }
    }
    return l;
}

struct Affix {
    Flag flag = 0;
    bool cross = false;
    std::string strip;
    std::string appnd;
    std::vector<Flag> cont;  // sorted
    std::string cond;        // as Hunspell stores it (suffixes: reversed)
    int numconds = 0;
};

bool testaff(const std::vector<Flag>& flags, Flag f) {
    return std::binary_search(flags.begin(), flags.end(), f);
}

// PfxEntry::test_condition
bool pfx_condition(const Affix& a, const std::string& s) {
    if (a.numconds == 0) return true;
    const char* c = a.cond.c_str();
    std::size_t p = 0;
    auto next = [&] {
        if (c[p]) ++p;
    };
    auto S = [&](std::size_t i) -> char { return i < s.size() ? s[i] : '\0'; };
    constexpr std::size_t npos = std::string::npos;
    std::size_t st = 0;
    std::size_t pos = npos;
    bool neg = false;
    bool ingroup = false;
    while (true) {
        switch (c[p]) {
            case '\0':
                return true;
            case '[':
                neg = false;
                ingroup = false;
                next();
                pos = st;
                break;
            case '^':
                next();
                neg = true;
                break;
            case ']':
                if (neg == ingroup) return false;
                pos = npos;
                next();
                if (!ingroup && st < s.size()) {
                    ++st;
                    while (st < s.size() && (s[st] & 0xc0) == 0x80) ++st;
                }
                if (st == s.size() && c[p]) return false;
                break;
            case '.':
                if (pos == npos) {
                    next();
                    ++st;
                    while (st < s.size() && (s[st] & 0xc0) == 0x80) ++st;
                    if (st == s.size() && c[p]) return false;
                    break;
                }
                [[fallthrough]];
            default:
                if (st < s.size() && s[st] == c[p]) {
                    ++st;
                    next();
                    if (S(st - 1) & 0x80) {  // multibyte character
                        while (c[p] && (c[p] & 0xc0) == 0x80) {
                            if (c[p] != S(st)) {
                                if (pos == npos) return false;
                                st = pos;
                                break;
                            }
                            next();
                            ++st;
                        }
                        if (pos != npos && st != pos) {
                            ingroup = true;
                            while (c[p] && c[p] != ']') next();
                        }
                    } else if (pos != npos) {
                        ingroup = true;
                        while (c[p] && c[p] != ']') next();
                    }
                } else if (pos != npos) {
                    next();
                } else {
                    return false;
                }
        }
        if (!c[p]) return true;
    }
}

// SfxEntry::test_condition, over the whole of w (the stripped word plus the restored strip).
// Hunspell may read the byte just before the word; that byte reads as 0 here.
bool sfx_condition(const Affix& a, const std::string& w) {
    if (a.numconds == 0) return true;
    const char* c = a.cond.c_str();
    std::size_t p = 0;
    auto next = [&] {
        if (c[p]) ++p;
    };
    const std::ptrdiff_t len = static_cast<std::ptrdiff_t>(w.size());
    auto W = [&](std::ptrdiff_t k) -> char { return k >= 0 && k < len ? w[static_cast<std::size_t>(k)] : '\0'; };
    std::ptrdiff_t st = len - 1;
    bool has_pos = false;
    std::ptrdiff_t pos = 0;
    bool neg = false;
    bool ingroup = false;
    int i = 1;
    while (true) {
        switch (c[p]) {
            case '\0':
                return true;
            case '[':
                next();
                pos = st;
                has_pos = true;
                break;
            case '^':
                next();
                neg = true;
                break;
            case ']':
                if (!neg && !ingroup) return false;
                ++i;
                if (!ingroup) {
                    for (; st >= 0 && (W(st) & 0xc0) == 0x80; --st) {
                    }
                    --st;
                }
                has_pos = false;
                neg = false;
                ingroup = false;
                next();
                if (st < 0 && c[p]) return false;
                break;
            case '.':
                if (!has_pos) {
                    next();
                    for (--st; st >= 0 && (W(st) & 0xc0) == 0x80; --st) {
                    }
                    if (st < 0) return !c[p];
                    if (W(st) & 0x80) {  // head of the UTF-8 character
                        --st;
                        if (st < 0) return !c[p];
                    }
                    break;
                }
                [[fallthrough]];
            default:
                if (W(st) == c[p]) {
                    next();
                    if (W(st) & 0x80) {
                        --st;
                        while (c[p] && st >= 0) {
                            if (c[p] != W(st)) {
                                if (!has_pos) return false;
                                st = pos;
                                break;
                            }
                            if ((c[p] & 0xc0) != 0x80) break;  // first byte of the character
                            next();
                            --st;
                        }
                        if (has_pos && st != pos) {
                            if (neg) return false;
                            if (i == a.numconds) return true;
                            ingroup = true;
                            while (c[p] && c[p] != ']') next();
                            --st;
                        }
                        if (c[p] && c[p] != ']') next();
                    } else if (has_pos) {
                        if (neg) return false;
                        if (i == a.numconds) return true;
                        ingroup = true;
                        while (c[p] && c[p] != ']') next();
                        --st;
                    }
                    if (!has_pos) {
                        ++i;
                        --st;
                    }
                    if (st < 0 && c[p] && c[p] != ']') return false;
                } else if (has_pos) {
                    next();
                } else {
                    return false;
                }
        }
        if (!c[p]) return true;
    }
}

// --- The dictionary ---------------------------------------------------------------------------

struct Homonym {
    std::uint32_t off = 0;  // flags: pool[off, off + len), sorted
    std::uint32_t len = 0;
    std::int32_t next = -1;
};

struct Dict {
    std::vector<Affix> pfx, sfx;
    std::unordered_map<std::string, std::vector<std::uint32_t>> pfx_by_appnd, sfx_by_appnd;
    std::size_t max_pfx = 0, max_sfx = 0;
    std::bitset<65536> contclasses;
    bool havecontclass = false;

    std::vector<Flag> pool;
    std::vector<Homonym> homs;
    std::unordered_map<std::string_view, std::int32_t> words;  // first homonym
    std::deque<std::string> owned;                              // hidden word spellings

    const Homonym* first(std::string_view w) const {
        auto it = words.find(w);
        return it == words.end() ? nullptr : &homs[static_cast<std::size_t>(it->second)];
    }
    const Homonym* next(const Homonym* h) const {
        return h->next < 0 ? nullptr : &homs[static_cast<std::size_t>(h->next)];
    }
    bool has(const Homonym* h, Flag f) const {
        return std::binary_search(pool.begin() + h->off, pool.begin() + h->off + h->len, f);
    }
};

std::vector<std::string_view> lines_of(std::string_view data) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < data.size()) {
        std::size_t e = data.find('\n', i);
        if (e == std::string_view::npos) e = data.size();
        std::string_view line = data.substr(i, e - i);
        // mychomp
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        i = e + 1;
    }
    return out;
}

// mystrsep: pieces separated by spaces and tabs
std::vector<std::string_view> pieces(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (true) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        std::size_t e = i;
        while (e < line.size() && line[e] != ' ' && line[e] != '\t') ++e;
        out.push_back(line.substr(i, e - i));
        i = e;
    }
    return out;
}

Flag decode_flag(std::string_view s) {
    const U16 u = u8_u16(s);
    return u.empty() ? 0 : u[0];
}

std::vector<Flag> decode_flags(std::string_view s) {
    U16 u = u8_u16(s);
    std::sort(u.begin(), u.end());
    return u;
}

void parse_aff(Dict& d, std::string_view data) {
    const auto lines = lines_of(data);
    for (std::size_t ln = 0; ln < lines.size(); ++ln) {
        const std::string_view line = lines[ln];
        const bool is_pfx = line.substr(0, 3) == "PFX";
        if (!is_pfx && line.substr(0, 3) != "SFX") continue;
        const auto head = pieces(line);
        if (head.size() < 4) fail("aff line " + std::to_string(ln + 1) + ": missing data");
        const Flag aflag = decode_flag(head[1]);
        const bool cross = head[2].front() == 'Y';
        const int n = std::atoi(std::string(head[3]).c_str());
        if (n <= 0) fail("aff line " + std::to_string(ln + 1) + ": bad entry number");
        for (int k = 0; k < n; ++k) {
            if (++ln >= lines.size()) fail("aff: truncated affix table");
            const auto pc = pieces(lines[ln]);
            if (pc.size() < 4 || decode_flag(pc[1]) != aflag)
                fail("aff line " + std::to_string(ln + 1) + ": affix is corrupt");
            Affix a;
            a.flag = aflag;
            a.cross = cross;
            a.strip = std::string(pc[2]);
            if (a.strip == "0") a.strip.clear();
            const std::string_view ap = pc[3];
            const std::size_t slash = ap.find('/');
            if (slash != std::string_view::npos) {
                a.appnd = std::string(ap.substr(0, slash));
                a.cont = decode_flags(ap.substr(slash + 1));
                d.havecontclass = true;
                for (const Flag f : a.cont) d.contclasses.set(f);
            } else {
                a.appnd = std::string(ap);
            }
            if (a.appnd == "0") a.appnd.clear();
            std::string chunk = pc.size() > 4 ? std::string(pc[4]) : std::string(".");
            // redundant_condition (UTF-8 mode only compares the strings)
            if (!a.strip.empty() && chunk != ".") {
                const std::size_t sl = a.strip.size(), cl = chunk.size();
                const bool redundant = is_pfx ? a.strip.compare(0, cl, chunk) == 0
                                              : (sl >= cl && a.strip.compare(sl - cl, std::string::npos, chunk) == 0);
                if (redundant) chunk = ".";
            }
            if (!is_pfx) {
                std::reverse(chunk.begin(), chunk.end());
                reverse_condition(chunk);
            }
            if (chunk != ".") {
                a.numconds = condlen(chunk);
                a.cond = chunk;
            }
            auto& vec = is_pfx ? d.pfx : d.sfx;
            auto& idx = is_pfx ? d.pfx_by_appnd : d.sfx_by_appnd;
            auto& mx = is_pfx ? d.max_pfx : d.max_sfx;
            idx[a.appnd].push_back(static_cast<std::uint32_t>(vec.size()));
            mx = std::max(mx, a.appnd.size());
            vec.push_back(std::move(a));
        }
    }
}

// HashMgr::add_word (homonyms; a hidden word is replaced by a later real one)
void add_word(Dict& d, std::string_view word, const std::vector<Flag>& flags, bool hidden) {
    auto it = d.words.find(word);
    if (it != d.words.end()) {
        if (hidden) return;
        std::int32_t last = it->second;
        while (d.homs[static_cast<std::size_t>(last)].next >= 0) last = d.homs[static_cast<std::size_t>(last)].next;
        Homonym& lh = d.homs[static_cast<std::size_t>(last)];
        const auto off = static_cast<std::uint32_t>(d.pool.size());
        d.pool.insert(d.pool.end(), flags.begin(), flags.end());
        if (d.has(&lh, kOnlyUpcase)) {  // remove the hidden homonym
            lh.off = off;
            lh.len = static_cast<std::uint32_t>(flags.size());
            return;
        }
        lh.next = static_cast<std::int32_t>(d.homs.size());
        d.homs.push_back({off, static_cast<std::uint32_t>(flags.size()), -1});
        return;
    }
    const auto off = static_cast<std::uint32_t>(d.pool.size());
    d.pool.insert(d.pool.end(), flags.begin(), flags.end());
    d.words.emplace(word, static_cast<std::int32_t>(d.homs.size()));
    d.homs.push_back({off, static_cast<std::uint32_t>(flags.size()), -1});
}

void parse_dic(Dict& d, std::string_view data) {
    const auto lines = lines_of(data);
    if (lines.empty() || std::atoi(std::string(lines[0]).c_str()) <= 0) fail("dic: bad word count");
    d.words.reserve(lines.size() * 2);
    d.homs.reserve(lines.size() + lines.size() / 4);
    for (std::size_t ln = 1; ln < lines.size(); ++ln) {
        std::string_view ts = lines[ln];
        // morphological fields: " xx:" or a tab
        std::size_t dp = 0;
        while ((dp = ts.find(':', dp)) != std::string_view::npos) {
            if (dp > 3 && (ts[dp - 3] == ' ' || ts[dp - 3] == '\t')) {
                for (dp -= 3; dp > 0 && (ts[dp - 1] == ' ' || ts[dp - 1] == '\t'); --dp) {
                }
                dp = dp == 0 ? std::string_view::npos : dp + 1;
                break;
            }
            ++dp;
        }
        const std::size_t tab = ts.find('\t');
        if (tab != std::string_view::npos && (dp == std::string_view::npos || tab < dp)) dp = tab + 1;
        if (dp != std::string_view::npos) ts = ts.substr(0, dp - 1);
        if (ts.find("\\/") != std::string_view::npos) fail("dic: escaped slash is not supported");
        std::size_t ap = ts.find('/', 1);
        std::vector<Flag> flags;
        std::string_view word = ts;
        if (ap != std::string_view::npos && ap != ts.size()) {
            flags = decode_flags(ts.substr(ap + 1));
            word = ts.substr(0, ap);
        }
        add_word(d, word, flags, false);
        // add_hidden_capitalized_word
        const U16 w16 = u8_u16(word);
        const CapType ct = w16.empty() ? kNoCap : captype(w16);
        if (ct == kHuhCap || ct == kHuhInitCap || (ct == kAllCap && !flags.empty())) {
            std::vector<Flag> f2 = flags;
            f2.push_back(kOnlyUpcase);
            std::sort(f2.begin(), f2.end());
            U16 h = w16;
            mkallsmall(h);
            mkinitcap(h);
            d.owned.push_back(u16_u8(h));
            add_word(d, d.owned.back(), f2, true);
        }
    }
}

// --- Lookup (affixmgr.cxx / affentry.cxx) -----------------------------------------------------

constexpr int kXProduct = 1;

class Checker {
public:
    explicit Checker(const Dict& d) : d_(d) {}

    const Homonym* affix_check(const std::string& word) const {
        const int len = static_cast<int>(word.size());
        if (const Homonym* h = prefix_check(word, len)) return h;
        if (const Homonym* h = suffix_check(word, len, 0, nullptr, 0)) return h;
        if (d_.havecontclass) {
            if (const Homonym* h = suffix_check_twosfx(word, len, 0, nullptr)) return h;
            if (const Homonym* h = prefix_check_twosfx(word, len)) return h;
        }
        return nullptr;
    }

private:
    const Dict& d_;

    template <class F>
    void for_prefixes(const std::string& word, int len, F&& f) const {
        const std::size_t mx = std::min<std::size_t>(d_.max_pfx, static_cast<std::size_t>(len));
        for (std::size_t k = 0; k <= mx; ++k) {
            auto it = d_.pfx_by_appnd.find(word.substr(0, k));
            if (it == d_.pfx_by_appnd.end()) continue;
            for (const auto i : it->second)
                if (f(d_.pfx[i])) return;
        }
    }

    template <class F>
    void for_suffixes(const std::string& word, int len, F&& f) const {
        const std::size_t mx = std::min<std::size_t>(d_.max_sfx, static_cast<std::size_t>(len));
        for (std::size_t k = 0; k <= mx; ++k) {
            auto it = d_.sfx_by_appnd.find(word.substr(static_cast<std::size_t>(len) - k, k));
            if (it == d_.sfx_by_appnd.end()) continue;
            for (const auto i : it->second)
                if (f(d_.sfx[i])) return;
        }
    }

    // PfxEntry::checkword
    const Homonym* pfx_checkword(const Affix& a, const std::string& word, int len) const {
        int tmpl = len - static_cast<int>(a.appnd.size());
        if (tmpl <= 0) return nullptr;
        std::string tmpword = a.strip;
        tmpword.append(word, a.appnd.size(), static_cast<std::size_t>(tmpl));
        if (!pfx_condition(a, tmpword)) return nullptr;
        tmpl += static_cast<int>(a.strip.size());
        for (const Homonym* h = d_.first(tmpword); h; h = d_.next(h))
            if (d_.has(h, a.flag)) return h;
        if (a.cross) return suffix_check(tmpword, tmpl, kXProduct, &a, 0);
        return nullptr;
    }

    const Homonym* prefix_check(const std::string& word, int len) const {
        const Homonym* rv = nullptr;
        for_prefixes(word, len, [&](const Affix& a) { return (rv = pfx_checkword(a, word, len)) != nullptr; });
        return rv;
    }

    // SfxEntry::checkword
    const Homonym* sfx_checkword(const Affix& a, const std::string& word, int len, int optflags, const Affix* ep,
                                 Flag cclass) const {
        if ((optflags & kXProduct) && !a.cross) return nullptr;
        const int tmpl = len - static_cast<int>(a.appnd.size());
        if (tmpl <= 0 || static_cast<std::size_t>(tmpl) + a.strip.size() < static_cast<std::size_t>(a.numconds))
            return nullptr;
        std::string tmp(word, 0, static_cast<std::size_t>(tmpl));
        tmp += a.strip;
        if (!sfx_condition(a, tmp)) return nullptr;
        for (const Homonym* h = d_.first(tmp); h; h = d_.next(h)) {
            if ((d_.has(h, a.flag) || (ep && testaff(ep->cont, a.flag))) &&
                (!(optflags & kXProduct) || (ep && d_.has(h, ep->flag)) || (ep && testaff(a.cont, ep->flag))) &&
                (!cclass || testaff(a.cont, cclass)))
                return h;
        }
        return nullptr;
    }

    const Homonym* suffix_check(const std::string& word, int len, int sfxopts, const Affix* ppfx,
                                Flag cclass) const {
        const Homonym* rv = nullptr;
        for_suffixes(word, len, [&](const Affix& a) {
            if (a.appnd.empty() && cclass && a.cont.empty()) return false;
            return (rv = sfx_checkword(a, word, len, sfxopts, ppfx, cclass)) != nullptr;
        });
        return rv;
    }

    // SfxEntry::check_twosfx
    const Homonym* sfx_check_twosfx(const Affix& a, const std::string& word, int len, int optflags,
                                    const Affix* ppfx) const {
        if ((optflags & kXProduct) && !a.cross) return nullptr;
        int tmpl = len - static_cast<int>(a.appnd.size());
        if (tmpl <= 0 || static_cast<std::size_t>(tmpl) + a.strip.size() < static_cast<std::size_t>(a.numconds))
            return nullptr;
        std::string tmp(word, 0, static_cast<std::size_t>(tmpl));
        tmp += a.strip;
        tmpl += static_cast<int>(a.strip.size());
        if (!sfx_condition(a, tmp)) return nullptr;
        if (ppfx) {
            if (testaff(a.cont, ppfx->flag)) return suffix_check(tmp, tmpl, 0, nullptr, a.flag);
            return suffix_check(tmp, tmpl, optflags, ppfx, a.flag);
        }
        return suffix_check(tmp, tmpl, 0, nullptr, a.flag);
    }

    const Homonym* suffix_check_twosfx(const std::string& word, int len, int sfxopts, const Affix* ppfx) const {
        const Homonym* rv = nullptr;
        for_suffixes(word, len, [&](const Affix& a) {
            if (!d_.contclasses.test(a.flag)) return false;
            return (rv = sfx_check_twosfx(a, word, len, sfxopts, ppfx)) != nullptr;
        });
        return rv;
    }

    // PfxEntry::check_twosfx
    const Homonym* pfx_check_twosfx(const Affix& a, const std::string& word, int len) const {
        int tmpl = len - static_cast<int>(a.appnd.size());
        if (tmpl <= 0 || static_cast<std::size_t>(tmpl) + a.strip.size() < static_cast<std::size_t>(a.numconds))
            return nullptr;
        std::string tmpword = a.strip;
        tmpword.append(word, a.appnd.size());
        if (!pfx_condition(a, tmpword)) return nullptr;
        tmpl += static_cast<int>(a.strip.size());
        if (a.cross) return suffix_check_twosfx(tmpword, tmpl, kXProduct, &a);
        return nullptr;
    }

    const Homonym* prefix_check_twosfx(const std::string& word, int len) const {
        const Homonym* rv = nullptr;
        for_prefixes(word, len, [&](const Affix& a) { return (rv = pfx_check_twosfx(a, word, len)) != nullptr; });
        return rv;
    }
};

// --- spell() (hunspell.cxx) -------------------------------------------------------------------

class Speller {
public:
    explicit Speller(const Dict& d) : d_(d), checker_(d) {}

    bool spell(const std::string& word) {
        if (std::find(stack_.begin(), stack_.end(), word) != stack_.end()) return false;
        stack_.push_back(word);
        const bool r = spell_internal(word);
        stack_.pop_back();
        return r;
    }

private:
    const Dict& d_;
    Checker checker_;
    std::vector<std::string> stack_;

    bool checkword(const std::string& word, bool initcap) const {
        if (word.empty()) return false;
        for (const Homonym* h = d_.first(word); h; h = d_.next(h))
            if (!(initcap && d_.has(h, kOnlyUpcase))) return true;
        const Homonym* h = checker_.affix_check(word);
        return h && !(initcap && d_.has(h, kOnlyUpcase));
    }

    static void mkallsmall2(std::string& s, U16& u) {
        mkallsmall(u);
        s = u16_u8(u);
    }
    static void mkinitcap2(std::string& s, U16& u) {
        mkinitcap(u);
        s = u16_u8(u);
    }

    bool spell_internal(const std::string& word) {
        if (word.size() >= kMaxWordUtf8) return false;
        // cleanword2
        std::size_t q = 0;
        while (q < word.size() && word[q] == ' ') ++q;
        std::size_t nl = word.size() - q;
        std::size_t abbv = 0;
        while (nl > 0 && word[q + nl - 1] == '.') {
            --nl;
            ++abbv;
        }
        if (nl == 0) return true;
        std::string scw = word.substr(q, nl);
        U16 sunicw = u8_u16(scw);
        const CapType ct = captype(sunicw);
        const std::size_t wl = scw.size();

        // numbers with dots, dashes and commas
        {
            enum { NBEGIN, NNUM, NSEP } nstate = NBEGIN;
            std::size_t i = 0;
            for (; i < wl; ++i) {
                if (scw[i] <= '9' && scw[i] >= '0') {
                    nstate = NNUM;
                } else if (scw[i] == ',' || scw[i] == '.' || scw[i] == '-') {
                    if (nstate == NSEP || i == 0) break;
                    nstate = NSEP;
                } else {
                    break;
                }
            }
            if (i == wl && nstate == NNUM) return true;
        }

        bool rv = false;
        switch (ct) {
            case kHuhCap:
            case kHuhInitCap:
            case kNoCap:
                rv = checkword(scw, false);
                if (abbv && !rv) rv = checkword(scw + ".", false);
                break;
            case kAllCap:
            case kInitCap: {
                if (ct == kAllCap) {
                    rv = checkword(scw, false);
                    if (rv) break;
                    if (abbv) {
                        rv = checkword(scw + ".", false);
                        if (rv) break;
                    }
                    const std::size_t apos = scw.find('\'');
                    if (apos != std::string::npos) {
                        mkallsmall2(scw, sunicw);
                        if (apos < scw.size() - 1) {
                            std::string part1 = scw.substr(0, apos + 1), part2 = scw.substr(apos + 1);
                            U16 part1u = u8_u16(part1), part2u = u8_u16(part2);
                            mkinitcap2(part2, part2u);
                            scw = part1 + part2;
                            sunicw = part1u;
                            sunicw.insert(sunicw.end(), part2u.begin(), part2u.end());
                            rv = checkword(scw, false);
                            if (rv) break;
                            mkinitcap2(scw, sunicw);
                            rv = checkword(scw, false);
                            if (rv) break;
                        }
                    }
                }
                const bool idot = scw.size() >= 2 && static_cast<unsigned char>(scw[0]) == 0xc4 &&
                                  static_cast<unsigned char>(scw[1]) == 0xb0;
                if (ct == kAllCap) {
                    mkallsmall2(scw, sunicw);
                    mkinitcap2(scw, sunicw);
                    if (idot) scw.replace(0, 1, "\xc4\xb0");
                }
                rv = checkword(scw, ct == kInitCap);
                if (rv || idot) break;
                mkallsmall2(scw, sunicw);
                std::string u8buffer(scw);
                mkinitcap2(scw, sunicw);
                rv = checkword(u8buffer, false);
                if (abbv && !rv) {
                    u8buffer.push_back('.');
                    rv = checkword(u8buffer, false);
                    if (!rv) {
                        u8buffer = scw;
                        u8buffer.push_back('.');
                        rv = checkword(u8buffer, ct == kInitCap);
                    }
                }
                break;
            }
        }
        if (rv) return true;
        return spell_breaks(scw);
    }

    // Recursive breaking at Hunspell's default BREAK points: "-", "^-", "-$".
    bool spell_breaks(std::string scw) {
        static const std::string kBreaks[] = {"-", "^-", "-$"};
        const std::size_t wl = scw.size();
        int nbr = 0;
        for (const auto& j : kBreaks) {
            std::size_t pos = 0;
            while ((pos = scw.find(j, pos)) != std::string::npos) {
                ++nbr;
                pos += j.size();
            }
        }
        if (nbr >= 10) return false;

        for (const auto& j : kBreaks) {
            const std::size_t plen = j.size();
            if (plen == 1 || plen > wl) continue;
            if (j[0] == '^' && scw.compare(0, plen - 1, j, 1, plen - 1) == 0 && spell(scw.substr(plen - 1)))
                return true;
            if (j[plen - 1] == '$' && scw.compare(wl - plen + 1, plen - 1, j, 0, plen - 1) == 0) {
                const std::string suffix = scw.substr(wl - plen + 1);
                scw.resize(wl - plen + 1);
                if (spell(scw)) return true;
                scw += suffix;
            }
        }
        for (const auto& j : kBreaks) {
            const std::size_t plen = j.size();
            std::size_t found = scw.find(j);
            if (found > 0 && found < wl - plen) {
                const std::size_t found2 = scw.find(j, found + 1);
                if (found2 > 0 && found2 < wl - plen) found = found2;
                if (!spell(scw.substr(found + plen))) continue;
                const std::string suffix = scw.substr(found);
                scw.resize(found);
                if (spell(scw)) return true;
                scw += suffix;
            }
        }
        for (const auto& j : kBreaks) {
            const std::size_t plen = j.size();
            const std::size_t found = scw.find(j);
            if (found > 0 && found < wl - plen) {
                if (!spell(scw.substr(found + plen))) continue;
                const std::string suffix = scw.substr(found);
                scw.resize(found);
                if (spell(scw)) return true;
                scw += suffix;
            }
        }
        return false;
    }
};

const Dict& dict() {
    static Dict d;
    static std::once_flag once;
    std::call_once(once, [] {
        const auto aff = data_file("hunspell/es_ES.aff");
        const auto dic = data_file("hunspell/es_ES.dic");
        if (!aff || !dic) fail("es_ES.aff or es_ES.dic is not embedded");
        if (aff->find("FLAG UTF-8") == std::string_view::npos || aff->find("SET UTF-8") == std::string_view::npos)
            fail("es_ES.aff must declare SET UTF-8 and FLAG UTF-8");
        parse_aff(d, *aff);
        parse_dic(d, *dic);
    });
    return d;
}

}  // namespace

bool in_dictionary(std::string_view word) {
    if (word.empty()) return false;
    bool letter = false;
    for (std::size_t i = 0; i < word.size();) {
        const auto dc = text::decode(word, i);
        if (text::is_space(dc.cp)) return false;
        if (text::is_letter(dc.cp)) letter = true;
        i += dc.len;
    }
    if (!letter) return false;
    Speller s(dict());
    return s.spell(std::string(word));
}

}  // namespace revisa::catalog
