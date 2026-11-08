// Protected spans. See protect.hpp for the contract.
//
// Rules decided here (tests/protect/*.jsonl):
//   - URL with a scheme (http, https, ftp, file) or starting with "www.": up to the next
//     whitespace or quote mark; trailing . , ; : ! ? … ' and unbalanced ) ] are left out.
//   - Bare domain or file name: ASCII labels joined by dots (ejemplo.es/ruta, informe.pdf),
//     with a word boundary on both sides. The last label must be a frequent TLD written in
//     lowercase (kTlds) or a frequent file extension in any case (kExtensions). A short list
//     instead of "any 2-6 letters" avoids protecting a missing space ("casa.luego",
//     "verdad.me voy"): TLDs that are common Spanish words (de, la, me, no, se, si, tu) are left
//     out on purpose, and "p. ej." or "etc." never match because a label cannot be empty.
//   - E-mail: local@domain with a dot in the domain and a TLD of 2+ letters.
//   - @mention and #hashtag: after a word boundary; a hashtag needs a letter ("#1" is a number).
//   - `inline code`: from a backtick to the next one, with something in between.
//   - Paths: /usr/bin, ~/x, ./a.txt, ../a, C:\Users\..., \\server\share, after whitespace,
//     an opening sign or the start; up to the next whitespace.
//   - Numbers with a unit (kUnits, longest first, with or without a space: 10 km, 5 %, 12h,
//     3,5 GB, 20 €), currency before the number ($20, € 5), and times (18:30, 18:30:05, 9:15 h).
//     Ordinals (5º) are not units: "5.º" is a correction the model may make.
//   - Special tokens: "<|" + 1-64 of [A-Za-z0-9_] + "|>", the shape of every control token of the
//     Qwen vocabulary (<|im_start|>, <|endoftext|>, <|vision_start|>…). A lone "<|" or
//     "<| a b |>" is not a token: parse_special would not turn it into a control id.

#include "core/protect/protect.hpp"

#include <algorithm>
#include <iterator>

#include "core/text/text.hpp"

namespace revisa::protect {

namespace {

constexpr std::string_view kTlds[] = {
    "com", "org",  "net",  "edu",  "gob",  "gov",  "mil",    "int",  "info", "biz",  "io",   "dev",
    "app", "ai",   "es",   "eu",   "cat",  "gal",  "eus",    "mx",   "ar",   "cl",   "co",   "pe",
    "uy",  "ve",   "ec",   "bo",   "py",   "cr",   "cu",     "gt",   "hn",   "ni",   "sv",   "pr",
    "uk",  "fr",   "it",   "pt",   "us",   "ca",   "br",     "nl",   "be",   "ch",   "at",   "ru",
    "cn",  "jp",   "tv",   "xyz",  "online", "site", "tech", "cloud", "blog", "shop",
};

constexpr std::string_view kExtensions[] = {
    "txt", "md",   "pdf",  "doc",  "docx", "odt", "rtf", "xls", "xlsx", "ods",  "csv",
    "ppt", "pptx", "odp",  "json", "xml",  "yaml", "yml", "toml", "ini", "cfg",  "conf",
    "log", "html", "htm",  "css",  "js",   "ts",  "py",  "c",   "cpp",  "h",    "hpp",
    "java", "rs",  "go",   "sh",   "exe",  "zip", "png", "jpg", "jpeg", "gif",  "mp4",
};

// Longest first, so that "km/h" wins over "km" and "kWh" over "kW".
constexpr std::string_view kUnits[] = {
    "km/h", "MB/s", "Mbps", "kbps", "Gbps", "GHz", "MHz", "kHz", "kWh", "mAh", "km²", "cm²", "m²",
    "m³",   "°C",   "ºC",   "°F",   "ºF",   "USD", "EUR", "min", "seg", "fps", "dpi", "Hz",  "kW",
    "km",   "cm",   "mm",   "kg",   "mg",   "ml",  "cl",  "dl",  "ms",  "GB",  "MB",  "KB",  "kB",
    "TB",   "Gb",   "Mb",   "px",   "pt",   "%",   "‰",   "€",   "$",   "£",   "m",   "g",   "l",
    "L",    "h",    "s",    "W",    "V",
};

bool is_word(char32_t c) { return text::is_letter(c) || text::is_digit(c) || text::is_mark(c); }
bool ascii_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool ascii_lower(char c) { return c >= 'a' && c <= 'z'; }

// The code point that ends right before byte i (U' ' at the start).
char32_t prev_cp(std::string_view s, std::size_t i) {
    if (i == 0) return U' ';
    std::size_t b = i - 1;
    while (b > 0 && i - b < 4 && (static_cast<unsigned char>(s[b]) & 0xC0) == 0x80) --b;
    const text::Decoded d = text::decode(s, b);
    return b + d.len == i ? d.cp : text::kReplacement;
}

char32_t cp_at(std::string_view s, std::size_t i) { return i < s.size() ? text::decode(s, i).cp : U' '; }

bool starts_with_ci(std::string_view s, std::size_t i, std::string_view p) {
    if (s.size() - i < p.size()) return false;
    for (std::size_t k = 0; k < p.size(); ++k) {
        char c = s[i + k];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != p[k]) return false;
    }
    return true;
}

// Stops a URL or path: whitespace, quotes, angle brackets and backticks.
bool ends_link(char32_t c) {
    return text::is_space(c) || c == U'"' || c == U'«' || c == U'»' || c == U'“' || c == U'”' || c == U'‘' ||
           c == U'’' || c == U'<' || c == U'>' || c == U'`';
}

// End of a URL-like run starting at i, without trailing punctuation or unbalanced brackets.
std::size_t link_end(std::string_view s, std::size_t i) {
    std::size_t e = i;
    while (e < s.size()) {
        const text::Decoded d = text::decode(s, e);
        if (ends_link(d.cp)) break;
        e += d.len;
    }
    for (;;) {
        if (e <= i) return e;
        const char32_t c = prev_cp(s, e);
        if (c == U'.' || c == U',' || c == U';' || c == U':' || c == U'!' || c == U'?' || c == U'\'') {
            --e;
            continue;
        }
        if (c == U'…') {
            e -= std::string_view("…").size();
            continue;
        }
        if (c == U')' || c == U']') {
            const char open = c == U')' ? '(' : '[';
            const std::string_view run = s.substr(i, e - i);
            const auto opens = std::count(run.begin(), run.end(), open);
            const auto closes = std::count(run.begin(), run.end(), static_cast<char>(c));
            if (closes > opens) {
                --e;
                continue;
            }
        }
        return e;
    }
}

class Finder {
public:
    explicit Finder(std::string_view s) : s_(s) {}

    std::vector<Span> run() {
        special_tokens();
        inline_code();
        for (std::size_t i = 0; i < s_.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s_[i]);
            if (c >= 0x80 && (c & 0xC0) == 0x80) continue;  // inside a code point
            std::size_t e = 0;
            if ((e = url(i)) || (e = path(i)) || (e = email(i)) || (e = tag(i)) || (e = number(i)) ||
                (e = domain(i))) {
                add(i, e);
                i = e - 1;
            }
        }
        std::sort(out_.begin(), out_.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
        std::vector<Span> merged;
        for (const Span& sp : out_) {
            if (!merged.empty() && sp.start <= merged.back().end)
                merged.back().end = std::max(merged.back().end, sp.end);
            else
                merged.push_back(sp);
        }
        return merged;
    }

private:
    void add(std::size_t a, std::size_t b) {
        if (b > a) out_.push_back({a, b});
    }

    void special_tokens() {
        for (std::size_t i = s_.find("<|"); i != std::string_view::npos; i = s_.find("<|", i + 1)) {
            std::size_t e = i + 2;
            while (e < s_.size() && e - i - 2 < 65 && (ascii_alnum(s_[e]) || s_[e] == '_')) ++e;
            const std::size_t n = e - i - 2;
            if (n >= 1 && n <= 64 && s_.substr(e, 2) == "|>") add(i, e + 2);
        }
    }

    void inline_code() {
        for (std::size_t i = s_.find('`'); i != std::string_view::npos;) {
            const std::size_t close = s_.find('`', i + 1);
            if (close == std::string_view::npos) return;
            if (close > i + 1) {
                add(i, close + 1);
                i = s_.find('`', close + 1);
            } else {
                i = close;  // "``": the second one may open a span
            }
        }
    }

    bool word_boundary_before(std::size_t i) const { return !is_word(prev_cp(s_, i)); }

    std::size_t url(std::size_t i) const {
        if (!word_boundary_before(i)) return 0;
        for (const std::string_view scheme : {"https://", "http://", "ftp://", "file://"}) {
            if (starts_with_ci(s_, i, scheme)) {
                const std::size_t e = link_end(s_, i + scheme.size());
                return e > i + scheme.size() ? e : 0;
            }
        }
        if (starts_with_ci(s_, i, "www.") && i + 4 < s_.size() && ascii_alnum(s_[i + 4])) return link_end(s_, i);
        return 0;
    }

    std::size_t path(std::size_t i) const {
        const char32_t p = prev_cp(s_, i);
        const bool boundary = text::is_space(p) || p == U'(' || p == U'[' || p == U'«' || p == U'"' ||
                              p == U'“' || p == U'\'' || p == U'‘' || p == U'¿' || p == U'¡';
        if (!boundary) return 0;
        std::size_t body = 0;
        if (s_.compare(i, 3, "../") == 0) body = i + 3;
        else if (s_.compare(i, 2, "./") == 0 || s_.compare(i, 2, "~/") == 0) body = i + 2;
        else if (s_.compare(i, 2, "\\\\") == 0) body = i + 2;
        else if (s_[i] == '/') body = i + 1;
        else if (i + 2 < s_.size() && ((s_[i] >= 'A' && s_[i] <= 'Z') || (s_[i] >= 'a' && s_[i] <= 'z')) &&
                 s_[i + 1] == ':' && s_[i + 2] == '\\')
            body = i + 3;
        if (body == 0 || body >= s_.size()) return 0;
        const char c = s_[body];
        if (!(ascii_alnum(c) || c == '.' || c == '_' || c == '-' || c == '~' ||
              static_cast<unsigned char>(c) >= 0x80))
            return 0;
        if (static_cast<unsigned char>(c) >= 0x80 && !is_word(cp_at(s_, body))) return 0;
        const std::size_t e = link_end(s_, i);
        return e > body ? e : 0;
    }

    static bool local_char(char c) {
        return ascii_alnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
    }

    // Labels [A-Za-z0-9-]+ (plus '_' for file names) joined by '.' from i; returns the end and the
    // start of the last label.
    std::pair<std::size_t, std::size_t> labels(std::size_t i, int& count, bool underscore = false) const {
        auto label_char = [&](char c) { return ascii_alnum(c) || c == '-' || (underscore && c == '_'); };
        std::size_t e = i, last = i;
        count = 0;
        for (;;) {
            std::size_t k = e;
            while (k < s_.size() && label_char(s_[k])) ++k;
            if (k == e) break;
            last = e;
            e = k;
            ++count;
            if (e + 1 < s_.size() && s_[e] == '.' && label_char(s_[e + 1])) {
                ++e;
                continue;
            }
            break;
        }
        return {e, last};
    }

    std::size_t email(std::size_t i) const {
        if (!local_char(s_[i]) || s_[i] == '.' || (i > 0 && local_char(s_[i - 1]))) return 0;
        if (!word_boundary_before(i)) return 0;
        std::size_t at = i;
        while (at < s_.size() && local_char(s_[at])) ++at;
        if (at >= s_.size() || s_[at] != '@') return 0;
        int n = 0;
        const auto [e, last] = labels(at + 1, n);
        if (n < 2 || e - last < 2) return 0;
        for (std::size_t k = last; k < e; ++k)
            if (!((s_[k] >= 'a' && s_[k] <= 'z') || (s_[k] >= 'A' && s_[k] <= 'Z'))) return 0;
        if (is_word(cp_at(s_, e))) return 0;
        return e;
    }

    // @mention or #hashtag.
    std::size_t tag(std::size_t i) const {
        if (s_[i] != '@' && s_[i] != '#') return 0;
        const char32_t p = prev_cp(s_, i);
        if (is_word(p) || p == U'@' || p == U'#' || p == U'.' || p == U'&' || p == U'_') return 0;
        std::size_t e = i + 1;
        bool letter = false;
        while (e < s_.size()) {
            const text::Decoded d = text::decode(s_, e);
            if (!(is_word(d.cp) || d.cp == U'_')) break;
            letter = letter || text::is_letter(d.cp) || d.cp == U'_';
            e += d.len;
        }
        if (e == i + 1) return 0;
        if (s_[i] == '#' && !letter) return 0;
        return e;
    }

    static bool is_unit_space(char32_t c) { return c == U' ' || c == 0xA0 || c == 0x202F || c == 0x2009; }

    // A unit at i followed by a word boundary; returns its end or 0.
    std::size_t unit(std::size_t i) const {
        for (const std::string_view u : kUnits) {
            if (s_.compare(i, u.size(), u) != 0) continue;
            const std::size_t e = i + u.size();
            if (u != "km/h" && u != "MB/s" && e < s_.size() && s_[e] == '/') continue;  // "m/s" is not "m"
            if (!is_word(cp_at(s_, e))) return e;
        }
        return 0;
    }

    std::size_t digits(std::size_t i) const {
        while (i < s_.size() && s_[i] >= '0' && s_[i] <= '9') ++i;
        return i;
    }

    // Number with a unit, currency + number, or a time.
    std::size_t number(std::size_t i) const {
        const char32_t p = prev_cp(s_, i);
        if (is_word(p) || p == U'.' || p == U',' || p == U':') return 0;
        std::size_t start_num = i;
        bool currency = false;
        for (const std::string_view c : {"$", "€", "£"}) {
            if (s_.compare(i, c.size(), c) == 0) {
                std::size_t k = i + c.size();
                if (k < s_.size() && is_unit_space(cp_at(s_, k))) k += text::decode(s_, k).len;
                if (k < s_.size() && s_[k] >= '0' && s_[k] <= '9') {
                    start_num = k;
                    currency = true;
                }
                break;
            }
        }
        if (start_num >= s_.size() || !(s_[start_num] >= '0' && s_[start_num] <= '9')) return 0;
        std::size_t e = digits(start_num);
        // Time: h:mm or h:mm:ss, hours of 1-2 digits.
        if (!currency && e - start_num <= 2 && e < s_.size() && s_[e] == ':' && digits(e + 1) == e + 3) {
            std::size_t t = e + 3;
            if (t < s_.size() && s_[t] == ':' && digits(t + 1) == t + 3) t += 3;
            if (is_word(cp_at(s_, t))) return 0;
            std::size_t k = t;
            if (k < s_.size() && is_unit_space(cp_at(s_, k))) k += text::decode(s_, k).len;
            if (k < s_.size() && s_[k] == 'h' && !is_word(cp_at(s_, k + 1))) return k + 1;
            return t;
        }
        while (e + 1 < s_.size() && (s_[e] == '.' || s_[e] == ',') && s_[e + 1] >= '0' && s_[e + 1] <= '9')
            e = digits(e + 1);
        if (currency) return is_word(cp_at(s_, e)) ? 0 : e;
        std::size_t k = e;
        if (k < s_.size() && is_unit_space(cp_at(s_, k))) k += text::decode(s_, k).len;
        return k < s_.size() ? unit(k) : 0;
    }

    std::size_t domain(std::size_t i) const {
        if (!ascii_alnum(s_[i])) return 0;
        const char32_t p = prev_cp(s_, i);
        if (is_word(p) || p == U'.' || p == U'@' || p == U'/' || p == U'-' || p == U'_' || p == U'\\') return 0;
        int n = 0;
        auto [e, last] = labels(i, n, true);
        if (n < 2) return 0;
        const std::string_view tld = s_.substr(last, e - last);
        if (is_word(cp_at(s_, e))) return 0;
        bool letter = false;
        for (std::size_t k = i; k < last; ++k) letter = letter || text::is_letter(static_cast<unsigned char>(s_[k]));
        if (!letter) return 0;
        const bool lower = std::all_of(tld.begin(), tld.end(), ascii_lower);
        const bool is_tld = lower && std::find(std::begin(kTlds), std::end(kTlds), tld) != std::end(kTlds);
        const std::string ext = text::lower(tld);
        const bool is_ext = std::find(std::begin(kExtensions), std::end(kExtensions), ext) != std::end(kExtensions);
        if (!is_tld && !is_ext) return 0;
        if (is_tld) {
            if (e + 1 < s_.size() && s_[e] == ':' && s_[e + 1] >= '0' && s_[e + 1] <= '9') e = digits(e + 1);
            if (e < s_.size() && s_[e] == '/') e = link_end(s_, i);
        }
        return e;
    }

    std::string_view s_;
    std::vector<Span> out_;
};

}  // namespace

std::vector<Span> protect(std::string_view segment) { return Finder(segment).run(); }

bool fully_protected(std::string_view segment, const std::vector<Span>& spans) {
    std::size_t next = 0;  // index of the first span that does not end before i
    for (std::size_t i = 0; i < segment.size();) {
        while (next < spans.size() && spans[next].end <= i) ++next;
        if (next < spans.size() && spans[next].start <= i) {
            i = spans[next].end;
            continue;
        }
        const text::Decoded d = text::decode(segment, i);
        if (is_word(d.cp)) return false;
        i += d.len;
    }
    return true;
}

bool has_special_token(std::string_view segment) {
    for (std::size_t i = segment.find("<|"); i != std::string_view::npos; i = segment.find("<|", i + 1)) {
        std::size_t e = i + 2;
        while (e < segment.size() && e - i - 2 <= 64 && (ascii_alnum(segment[e]) || segment[e] == '_')) ++e;
        const std::size_t n = e - i - 2;
        if (n >= 1 && n <= 64 && segment.substr(e, 2) == "|>") return true;
    }
    return false;
}

}  // namespace revisa::protect
