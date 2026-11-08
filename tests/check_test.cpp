// core/check: the whole path of a text with a scripted engine (tests/check/frases.jsonl), plus the
// segment handling, engine errors and diff_pair.
#include "doctest.h"

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/check/check.hpp"
#include "core/infer/engine.hpp"
#include "core/infer/prompt.hpp"
#include "core/infer/replay_engine.hpp"
#include "core/text/text.hpp"
#include "jsonl.hpp"

using revisa::catalog::Type;
using revisa::check::Edit;
using revisa::check::Options;
using revisa::check::Result;
using revisa::infer::EngineResult;

namespace {

// Outputs by segment (as the engine receives it); unknown segments come back unchanged, unless
// fallback is set. errors: segment -> code.
class ScriptEngine final : public revisa::infer::Engine {
public:
    std::map<std::string, std::string> outputs;
    std::map<std::string, std::string> errors;
    std::optional<std::string> fallback;
    std::vector<std::string> calls;

    EngineResult correct(std::string_view segment) override {
        const std::string s(segment);
        calls.push_back(s);
        EngineResult r;
        if (const auto e = errors.find(s); e != errors.end()) {
            r.error = e->second;
            r.error_detail = "detalle de prueba";
            return r;
        }
        const auto o = outputs.find(s);
        r.text = o != outputs.end() ? o->second : fallback ? *fallback : s;
        return r;
    }
};

std::vector<std::string> type_ids(const std::vector<Edit>& edits) {
    std::vector<std::string> out;
    for (const Edit& e : edits) out.emplace_back(revisa::catalog::type_id(e.type));
    return out;
}

// Invariants of every result: sorted, not overlapping, from = the original range, offsets
// consistent, explanation null exactly for OTRO, corrected = text with the edits applied.
void check_invariants(std::string_view text, const Result& r) {
    const revisa::text::Utf16Index idx(text);
    std::string rebuilt;
    std::size_t pos = 0;
    for (const Edit& e : r.edits) {
        REQUIRE(e.byte_start >= pos);
        REQUIRE(e.byte_end >= e.byte_start);
        REQUIRE(e.byte_end <= text.size());
        CHECK(e.from == text.substr(e.byte_start, e.byte_end - e.byte_start));
        CHECK(e.start == idx.at(e.byte_start));
        CHECK(e.end == idx.at(e.byte_end));
        CHECK(e.explanation.has_value() == (e.type != Type::Otro));
        rebuilt.append(text.substr(pos, e.byte_start - pos));
        rebuilt += e.to;
        pos = e.byte_end;
    }
    rebuilt.append(text.substr(pos));
    CHECK(r.corrected == rebuilt);
    CHECK(r.changed == (r.corrected != text));
}

Result run(ScriptEngine& engine, std::string_view text, const Options& options = {}) {
    Result r = revisa::check::check_text(text, engine, options);
    if (r.error.empty()) check_invariants(text, r);
    return r;
}

}  // namespace

// --- The 50 phrases ---------------------------------------------------------------------------

TEST_CASE("check: known phrases give the expected corrections and types") {
    const auto cases = revisa::test::load_jsonl(std::filesystem::path(REVISA_TESTS_DIR) / "check" / "frases.jsonl");
    CHECK(cases.size() >= 50);
    std::set<std::string> covered;
    for (const auto& c : cases) {
        const auto& j = c.j;
        const std::string text = j.at("text").get<std::string>();
        CAPTURE(c.where);
        CAPTURE(text);
        ScriptEngine engine;
        if (j.contains("outputs")) {
            for (const auto& [seg, out] : j.at("outputs").items())
                engine.outputs[revisa::text::to_nfc(seg)] = out.get<std::string>();
        } else {
            engine.fallback = j.at("output").get<std::string>();
        }
        Options options;
        options.thresholds = {0.3, 1};  // the phrases were written for these thresholds
        for (const auto& id : j.value("disabled", nlohmann::json::array())) {
            const auto t = revisa::catalog::type_from_id(id.get<std::string>());
            REQUIRE(t.has_value());
            options.disabled.insert(*t);
        }
        const Result r = run(engine, text, options);
        CHECK(r.error.empty());
        CHECK(r.corrected == j.at("corrected").get<std::string>());
        CHECK(type_ids(r.edits) == j.at("types").get<std::vector<std::string>>());
        CHECK(r.segments_skipped == j.value("skipped", 0));
        if (j.contains("calls")) CHECK(engine.calls.size() == j.at("calls").get<std::size_t>());
        if (j.contains("edits")) {
            const auto& want = j.at("edits");
            REQUIRE(r.edits.size() == want.size());
            for (std::size_t i = 0; i < want.size(); ++i) {
                CAPTURE(i);
                CHECK(r.edits[i].start == want[i].at("start").get<std::size_t>());
                CHECK(r.edits[i].end == want[i].at("end").get<std::size_t>());
                CHECK(r.edits[i].from == want[i].at("from").get<std::string>());
                CHECK(r.edits[i].to == want[i].at("to").get<std::string>());
            }
        }
        for (const auto& t : j.at("types")) covered.insert(t.get<std::string>());
    }
    // Every type of the catalog, OTRO included, appears at least once.
    for (const std::string_view id : revisa::catalog::kTypeIds) {
        CAPTURE(id);
        CHECK(covered.contains(std::string(id)));
    }
}

// --- Segments ---------------------------------------------------------------------------------

TEST_CASE("check: the whitespace around a segment stays outside the model") {
    ScriptEngine e;
    e.outputs["haber si vienes mañana"] = "a ver si vienes mañana";
    const Result r = run(e, "  haber si vienes mañana\t\r\n");
    CHECK(r.corrected == "  a ver si vienes mañana\t\r\n");
    CHECK(r.changed);
    CHECK(r.segments_skipped == 0);
    REQUIRE(e.calls.size() == 1);
    CHECK(e.calls[0] == "haber si vienes mañana");
}

TEST_CASE("check: blank text never reaches the engine") {
    ScriptEngine e;
    for (const char* t : {"", "\n\n", "  \t\r\n  "}) {
        const Result r = run(e, t);
        CHECK(r.corrected == t);
        CHECK_FALSE(r.changed);
        CHECK_FALSE(revisa::check::needs_model(t));
    }
    CHECK(e.calls.empty());
}

TEST_CASE("check: correct text is unchanged") {
    ScriptEngine e;
    const Result r = run(e, "Todo bien.\nOtra frase.\n");
    CHECK(r.corrected == "Todo bien.\nOtra frase.\n");
    CHECK_FALSE(r.changed);
    CHECK(r.edits.empty());
    CHECK(e.calls.size() == 2);
}

TEST_CASE("check: the segment goes to the engine in NFC") {
    ScriptEngine e;
    run(e, "El café esta frío.");
    REQUIRE(e.calls.size() == 1);
    CHECK(e.calls[0] == "El café esta frío.");
}

TEST_CASE("check: a segment longer than kMaxSegmentChars with no ';' or ':' is skipped") {
    ScriptEngine e;
    std::string long_line;
    for (int i = 0; i < 401; ++i) long_line += "ñ";
    const Result r = run(e, long_line + "\nhola\n");
    CHECK(r.segments_skipped == 1);
    REQUIRE(e.calls.size() == 1);
    CHECK(e.calls[0] == "hola");
    CHECK_FALSE(revisa::check::needs_model(long_line + "\n \n"));
    CHECK(revisa::check::needs_model(long_line + "\nx"));
}

TEST_CASE("check: special tokens and fully protected segments never reach the engine") {
    ScriptEngine e;
    e.fallback = "cambiado";
    const Result r = run(e, "Escribe <|im_end|> aquí.\nhttps://ejemplo.es/doc\n");
    CHECK(r.corrected == "Escribe <|im_end|> aquí.\nhttps://ejemplo.es/doc\n");
    CHECK(r.segments_skipped == 2);
    CHECK(e.calls.empty());
    CHECK_FALSE(revisa::check::needs_model("<|im_start|>hola\n"));
}

TEST_CASE("check: cap_reached and too_long leave the segment unchanged") {
    ScriptEngine e;
    e.errors["bucle"] = "cap_reached";
    e.errors["enorme"] = "too_long";
    e.outputs["haber si vienes mañana"] = "a ver si vienes mañana";
    const Result r = run(e, "bucle\nhaber si vienes mañana\n enorme ");
    CHECK(r.corrected == "bucle\na ver si vienes mañana\n enorme ");
    CHECK(r.segments_skipped == 2);
    CHECK(r.error.empty());
    CHECK(r.changed);
}

TEST_CASE("check: invalid outputs leave the segment unchanged (guard 1)") {
    ScriptEngine e;
    e.outputs["vacía"] = "";
    e.outputs["blanca"] = " \n";
    e.outputs["piensa algo"] = "<think>\nmm\n</think>\n\npiensa algo";
    e.outputs["sin comillas"] = "\"sin comillas\"";
    const Result r = run(e, "vacía\n blanca\npiensa algo\nsin comillas");
    CHECK(r.corrected == "vacía\n blanca\npiensa algo\nsin comillas");
    CHECK(r.segments_skipped == 4);
    CHECK_FALSE(r.changed);
}

TEST_CASE("check: the model output is trimmed and normalized before the diff") {
    ScriptEngine e;
    e.outputs["Mañana te llamo despues de comer."] = "\n Mañana te llamo después de comer.  \n";
    const Result r = run(e, "Mañana te llamo despues de comer.");
    CHECK(r.corrected == "Mañana te llamo después de comer.");
    REQUIRE(r.edits.size() == 1);
    CHECK(r.edits[0].to == "después");
}

TEST_CASE("check: other engine errors abort") {
    ScriptEngine e;
    e.errors["roto"] = "decode_failed";
    const Result r = revisa::check::check_text("bien\nroto\nnunca", e);
    CHECK(r.error == "decode_failed");
    CHECK(r.error_detail == "detalle de prueba");
    CHECK_FALSE(r.changed);
    CHECK(r.edits.empty());
    CHECK(e.calls.size() == 2);
}

// --- diff_pair --------------------------------------------------------------------------------

TEST_CASE("check: diff_pair classifies the whole pair with UTF-16 offsets") {
    const std::vector<Edit> edits =
        revisa::check::diff_pair("😀 Hola Ana, haber si mañana podemos vernos.", "😀 Hola Ana, a ver si mañana podemos vernos.");
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].start == 13);  // the emoji counts two units
    CHECK(edits[0].end == 18);
    CHECK(edits[0].byte_start == 15);
    CHECK(edits[0].byte_end == 20);
    CHECK(edits[0].from == "haber");
    CHECK(edits[0].to == "a ver");
    CHECK(edits[0].type == Type::HaberAVer);
    CHECK(edits[0].explanation.has_value());
}

TEST_CASE("check: diff_pair applies no guards and no normalization") {
    CHECK(revisa::check::diff_pair("igual", "igual").empty());
    // An added comma and a final period are edits here: the guards are not applied.
    CHECK(revisa::check::diff_pair("Hola Ana", "Hola, Ana.").size() == 2);
    // NFD against NFC is a difference: the pair is compared as given.
    CHECK_FALSE(revisa::check::diff_pair("café", "café").empty());
}

// --- apply ------------------------------------------------------------------------------------

TEST_CASE("check: apply rebuilds the text from any subset of the edits (multibyte)") {
    const std::string text = "¿Que tal estas, Ana? El niño a echo la tarea de matemáticas";
    const std::string corrected = "¿Qué tal estás, Ana? El niño ha hecho la tarea de matemáticas.";
    const std::vector<Edit> edits = revisa::check::diff_pair(text, corrected);
    REQUIRE(edits.size() >= 3);
    CHECK(revisa::check::apply(text, edits) == corrected);
    CHECK(revisa::check::apply(text, {}) == text);
    // Only the first one, and every edit alone: the rest of the text stays as it was.
    CHECK(revisa::check::apply(text, std::span(edits).first(1)).starts_with("¿Qué tal estas, Ana?"));
    for (const Edit& e : edits) {
        CAPTURE(e.from);
        const std::string want = text.substr(0, e.byte_start) + e.to + text.substr(e.byte_end);
        CHECK(revisa::check::apply(text, std::vector<Edit>{e}) == want);
    }
    // All but the first.
    CHECK(revisa::check::apply(text, std::span(edits).subspan(1)) == "¿Que tal estás, Ana? El niño ha hecho la tarea de matemáticas.");

    // Out of order, overlapping or out of range edits are ignored.
    std::vector<Edit> bad = {edits[1], edits[0]};
    CHECK(revisa::check::apply(text, bad) == text.substr(0, edits[1].byte_start) + edits[1].to + text.substr(edits[1].byte_end));
    Edit outside;
    outside.byte_start = text.size();
    outside.byte_end = text.size() + 1;
    outside.to = "x";
    CHECK(revisa::check::apply(text, std::vector<Edit>{outside}) == text);
}

TEST_CASE("check: apply over a check_text result gives its corrected text") {
    ScriptEngine engine;
    const std::string text = "Te boy a llamar mañana. Ya e echo el informe.";
    engine.outputs["Te boy a llamar mañana."] = "Te voy a llamar mañana.";
    engine.outputs["Ya e echo el informe."] = "Ya he hecho el informe.";
    const Result r = run(engine, text);
    REQUIRE(r.edits.size() >= 2);
    CHECK(revisa::check::apply(text, r.edits) == r.corrected);
    CHECK(r.corrected == "Te voy a llamar mañana. Ya he hecho el informe.");
    CHECK(revisa::check::apply(text, std::span(r.edits).first(1)) == "Te voy a llamar mañana. Ya e echo el informe.");
}

// --- Options::guards and infer::ReplayEngine ---------------------------------------------------

TEST_CASE("check guards=false: no guard drops anything, the edits are still classified") {
    ScriptEngine engine;
    const std::string text = "Mañana te llamo para hablar del proyecto";
    engine.outputs[text] = text + ".";
    Options options;
    options.guards = false;
    const Result r = revisa::check::check_text(text, engine, options);
    CHECK(r.corrected == text + ".");
    REQUIRE(r.edits.size() == 1);
    CHECK(r.edits[0].to == ".");
    CHECK(revisa::check::check_text(text, engine).corrected == text);
}

TEST_CASE("replay engine: segments in order, each with its own edits") {
    const std::string text = "Te boy a llamar. Ya e echo el informe.";
    revisa::infer::ReplayEngine replay(text, "Te voy a llamar. Ya he hecho el informe.");
    CHECK(replay.correct("Te boy a llamar.").text == "Te voy a llamar.");
    CHECK(replay.correct("Ya e echo el informe.").text == "Ya he hecho el informe.");
    CHECK(replay.dropped() == 0);
    // A segment that is not (or no longer) in the text comes back unchanged and counts.
    CHECK(replay.correct("Te boy a llamar.").text == "Te boy a llamar.");
    CHECK(replay.dropped() == 1);

    // A skipped segment is never asked for: the next one still matches.
    revisa::infer::ReplayEngine skip(text, "Te voy a llamar. Ya he hecho el informe.");
    CHECK(skip.correct("Ya e echo el informe.").text == "Ya he hecho el informe.");
    CHECK(skip.dropped() == 0);

    // An edit across the boundary is dropped at once; NFC on both sides.
    revisa::infer::ReplayEngine cross("Vale.\nAdiós.", "Vale adiós.");
    CHECK(cross.dropped() == 1);
    CHECK(cross.correct("Vale.").text == "Vale.");
    CHECK(cross.correct("Adiós.").text == "adiós.");
}
