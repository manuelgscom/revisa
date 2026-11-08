// lsp: framing, positions in the three encodings, the Markdown masker and in-process sessions
// with a scripted engine.
#include "doctest.h"

#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "core/infer/engine.hpp"
#include "lsp/lsp.hpp"
#include "lsp/markdown.hpp"
#include "lsp/position.hpp"

using json = nlohmann::json;
using revisa::lsp::Encoding;
using revisa::lsp::LineMap;
using revisa::lsp::Position;

namespace {

// Outputs by trimmed segment; others come back unchanged. error: every call fails with it.
class ScriptEngine final : public revisa::infer::Engine {
public:
    std::map<std::string, std::string> outputs;
    std::string error;

    revisa::infer::EngineResult correct(std::string_view segment) override {
        std::lock_guard lock(mutex_);
        calls_.emplace_back(segment);
        revisa::infer::EngineResult r;
        if (!error.empty()) {
            r.error = error;
            r.error_detail = "detalle";
            return r;
        }
        const auto it = outputs.find(std::string(segment));
        r.text = it != outputs.end() ? it->second : std::string(segment);
        return r;
    }

    std::vector<std::string> calls() {
        std::lock_guard lock(mutex_);
        return calls_;
    }

private:
    std::mutex mutex_;
    std::vector<std::string> calls_;
};

std::string frame(const json& j) {
    const std::string body = j.dump();
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

json request(int id, const std::string& method, const json& params) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

json notification(const std::string& method, const json& params) {
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}

std::vector<json> messages(const std::string& out) {
    std::istringstream in(out);
    std::vector<json> all;
    while (const auto body = revisa::lsp::read_message(in)) all.push_back(json::parse(*body));
    return all;
}

const json* response(const std::vector<json>& all, int id) {
    for (const json& m : all) {
        if (m.contains("id") && m["id"] == id && !m.contains("method")) return &m;
    }
    return nullptr;
}

std::vector<json> notifications(const std::vector<json>& all, const std::string& method) {
    std::vector<json> out;
    for (const json& m : all) {
        if (m.value("method", "") == method) out.push_back(m);
    }
    return out;
}

json pos(std::size_t line, std::size_t character) { return {{"line", line}, {"character", character}}; }

json initialize_params(const json& encodings) {
    json p = {{"processId", nullptr}, {"capabilities", json::object()}};
    if (!encodings.is_null()) p["capabilities"]["general"]["positionEncodings"] = encodings;
    return p;
}

json open_params(const std::string& uri, const std::string& language, const std::string& text, int version = 1) {
    return {{"textDocument", {{"uri", uri}, {"languageId", language}, {"version", version}, {"text", text}}}};
}

json code_action_params(const std::string& uri, json start, json end) {
    return {{"textDocument", {{"uri", uri}}},
            {"range", {{"start", std::move(start)}, {"end", std::move(end)}}},
            {"context", {{"diagnostics", json::array()}}}};
}

const char* const kWrong = "😀👍 Ya e echo el informe.";
const char* const kRight = "😀👍 Ya he hecho el informe.";

}  // namespace

// --- Framing ----------------------------------------------------------------------------------

TEST_CASE("lsp framing: read and write") {
    std::ostringstream out;
    revisa::lsp::write_message(out, "{\"a\":\"ñ\"}");
    CHECK(out.str() == "Content-Length: 10\r\n\r\n{\"a\":\"ñ\"}");

    std::istringstream in(
        "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\ncontent-length: 2\r\n\r\n{}"
        "X-Other: 1\r\n\r\n"                    // no Content-Length: skipped
        "Content-Length:3\n\n[1]"               // bare \n tolerated
        "Content-Length: 10\r\n\r\n{\"cut");    // EOF inside the body
    CHECK(revisa::lsp::read_message(in) == "{}");
    CHECK(revisa::lsp::read_message(in) == "[1]");
    CHECK_FALSE(revisa::lsp::read_message(in).has_value());

    std::istringstream empty("");
    CHECK_FALSE(revisa::lsp::read_message(empty).has_value());
}

// --- Positions --------------------------------------------------------------------------------

TEST_CASE("lsp positions: UTF-16, UTF-8 and UTF-32 over several lines") {
    // Line 0: "á😀b" (á 2 bytes, 😀 4 bytes); \r\n; line 1: "x👍é"; \r; line 2: "fin"; \n; line 3: "".
    const std::string text = "á😀b\r\nx👍é\rfin\n";
    const LineMap map(text);
    REQUIRE(map.line_count() == 4);

    const std::size_t b_pos = std::string("á😀").size();    // 6: before "b"
    CHECK(map.position(b_pos, Encoding::Utf16) == Position{0, 3});
    CHECK(map.position(b_pos, Encoding::Utf8) == Position{0, 6});
    CHECK(map.position(b_pos, Encoding::Utf32) == Position{0, 2});

    const std::size_t e_pos = std::string("á😀b\r\nx👍").size();  // before "é"
    CHECK(map.position(e_pos, Encoding::Utf16) == Position{1, 3});
    CHECK(map.position(e_pos, Encoding::Utf8) == Position{1, 5});
    CHECK(map.position(e_pos, Encoding::Utf32) == Position{1, 2});

    const std::size_t fin = text.find("fin");
    CHECK(map.position(fin + 3, Encoding::Utf16) == Position{2, 3});
    CHECK(map.position(text.size(), Encoding::Utf16) == Position{3, 0});
    // Inside a line break: the line's end.
    CHECK(map.position(text.find('\n'), Encoding::Utf16) == Position{0, 4});

    for (const Encoding enc : {Encoding::Utf8, Encoding::Utf16, Encoding::Utf32}) {
        for (const std::size_t b : {std::size_t{0}, b_pos, e_pos, fin, text.size()}) {
            CHECK(map.byte(map.position(b, enc), enc) == b);
        }
    }
    // Past the end of a line, past the last line, inside a surrogate pair.
    CHECK(map.byte({0, 99}, Encoding::Utf16) == text.find('\r'));
    CHECK(map.byte({9, 0}, Encoding::Utf16) == text.size());
    CHECK(map.byte({0, 2}, Encoding::Utf16) == std::string("á").size());

    CHECK(revisa::lsp::parse_encoding("utf-32") == Encoding::Utf32);
    CHECK_FALSE(revisa::lsp::parse_encoding("latin1").has_value());
    CHECK(revisa::lsp::encoding_name(Encoding::Utf8) == "utf-8");
}

// --- Markdown masker --------------------------------------------------------------------------

namespace {

// mask_markdown keeps the length and the line breaks, and only writes spaces.
void check_mask_shape(const std::string& in, const std::string& out) {
    REQUIRE(out.size() == in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\n' || in[i] == '\r') CHECK(out[i] == in[i]);
        else CHECK((out[i] == in[i] || out[i] == ' '));
    }
}

std::string sp(std::size_t n) { return std::string(n, ' '); }

std::string masked(const std::string& in) {
    std::string out = revisa::lsp::mask_markdown(in);
    check_mask_shape(in, out);
    return out;
}

}  // namespace

TEST_CASE("lsp markdown masker") {
    using revisa::lsp::mask_markdown;

    SUBCASE("plain prose is untouched") {
        const std::string t = "# Título\n\nHola, ¿qué tal? Un *énfasis* y [un enlace].\n";
        CHECK(masked(t) == t);
    }
    SUBCASE("front matter") {
        CHECK(masked("---\ntitle: Hola\n---\nTexto.") == "   \n           \n   \nTexto.");
        CHECK(masked("---\r\na: b\r\n...\r\nTexto") == "   \r\n    \r\n   \r\nTexto");
        // Not closed: not front matter.
        CHECK(masked("---\nTexto") == "---\nTexto");
        // Only at the very start.
        CHECK(masked("Hola\n---\nx: y\n---\n") == "Hola\n---\nx: y\n---\n");
    }
    SUBCASE("fenced code blocks") {
        CHECK(masked("A.\n```cpp\nint x;\n```\nB.") == "A.\n      \n      \n   \nB.");
        CHECK(masked("A.\n~~~~\nx\n~~~\ny\n~~~~\nB.") == "A.\n    \n \n   \n \n    \nB.");
        CHECK(masked("A.\n  ```\ncódigo") == "A.\n     \n       ");  // unclosed: to the end
        // ``` with a backtick in the info string is not a fence: here, a code span.
        CHECK(masked("```a`b``` fin") == sp(9) + " fin");
    }
    SUBCASE("code spans") {
        CHECK(masked("Usa `git e echo` aquí.") == "Usa              aquí.");
        CHECK(masked("Doble ``a ` b`` fin") == "Doble " + sp(9) + " fin");
        CHECK(masked("Sin cerrar ` aquí") == "Sin cerrar ` aquí");
        // Never across a blank line.
        CHECK(masked("a `b\n\nc` d") == "a `b\n\nc` d");
    }
    SUBCASE("links, images, autolinks and HTML") {
        CHECK(masked("Ver [la guía](https://x.org/a_(b)) ya.") == "Ver [la guía]" + sp(21) + " ya.");
        CHECK(masked("![logo](img/l.png \"t\")") == "![logo]" + sp(15));
        CHECK(masked("Escribe a <https://x.org> hoy.") == "Escribe a                 hoy.");
        CHECK(masked("Un <span class=\"a\">texto</span>.") == "Un                 texto       .");
        CHECK(masked("Antes <!-- nota\nmás --> después") == "Antes " + sp(9) + "\n" + sp(8) + " después");
        CHECK(masked("Si a < b y c > d.") == "Si a < b y c > d.");
    }
}

TEST_CASE("lsp markdown masker: backslash escapes") {
    // An escaped backtick does not open a span: the second backtick is literal too (unclosed).
    const std::string t = "Escapado \\`no` es";
    CHECK(masked(t) == t);
}

// --- Sessions ---------------------------------------------------------------------------------

namespace {

struct Session {
    ScriptEngine engine;
    std::ostringstream out;
    std::unique_ptr<revisa::lsp::Server> server;

    Session() {
        engine.outputs[kWrong] = kRight;
        revisa::lsp::Options options;
        options.debounce = std::chrono::milliseconds(0);
        server = std::make_unique<revisa::lsp::Server>(out, engine, options);
    }
    bool send(const json& j) { return server->handle(j.dump()); }
    std::vector<json> all() {
        server->flush();
        return messages(out.str());
    }
};

}  // namespace

TEST_CASE("lsp session: diagnostics, code actions, shutdown and exit") {
    Session s;
    const std::string uri = "file:///tmp/nota.md";
    s.send(request(1, "initialize", initialize_params(json::array({"utf-8", "utf-16"}))));
    s.send(notification("initialized", json::object()));
    const std::string text = std::string(kWrong) + " Más.\nOtra línea.\n\nUsa `e echo` en código.";
    s.send(notification("textDocument/didOpen", open_params(uri, "markdown", text, 3)));
    s.send(notification("textDocument/didOpen", open_params("file:///tmp/a.cpp", "cpp", kWrong)));

    std::vector<json> all = s.all();
    const json* init = response(all, 1);
    REQUIRE(init != nullptr);
    const json& caps = (*init)["result"]["capabilities"];
    CHECK(caps["positionEncoding"] == "utf-16");
    CHECK(caps["textDocumentSync"]["change"] == 1);
    CHECK(caps["textDocumentSync"]["openClose"] == true);
    CHECK(caps["codeActionProvider"]["codeActionKinds"] == json::array({"quickfix"}));
    CHECK((*init)["result"]["serverInfo"]["name"] == "revisa");

    // The code span never reaches the engine.
    for (const std::string& call : s.engine.calls()) CHECK(call.find("echo` en") == std::string::npos);

    const std::vector<json> diags = notifications(all, "textDocument/publishDiagnostics");
    REQUIRE(diags.size() == 1);  // the C++ file is ignored
    const json& p = diags[0]["params"];
    CHECK(p["uri"] == uri);
    CHECK(p["version"] == 3);
    REQUIRE(p["diagnostics"].size() == 2);
    const json& d0 = p["diagnostics"][0];
    CHECK(d0["range"] == json{{"start", pos(0, 8)}, {"end", pos(0, 9)}});
    CHECK(d0["severity"] == 3);
    CHECK(d0["source"] == "revisa");
    CHECK(d0["code"] == "VERBO_HABER_H");
    CHECK(d0["message"].get<std::string>().find("«he»") != std::string::npos);
    CHECK(p["diagnostics"][1]["range"] == json{{"start", pos(0, 10)}, {"end", pos(0, 14)}});

    // Cursor on "e": its quickfix plus the whole paragraph.
    s.send(request(2, "textDocument/codeAction", code_action_params(uri, pos(0, 8), pos(0, 8))));
    all = s.all();
    const json* ca = response(all, 2);
    REQUIRE(ca != nullptr);
    const json& actions = (*ca)["result"];
    REQUIRE(actions.size() == 2);
    CHECK(actions[0]["title"] == "Cambiar a «he»");
    CHECK(actions[0]["kind"] == "quickfix");
    CHECK(actions[0]["isPreferred"] == true);
    const json& edit0 = actions[0]["edit"]["changes"][uri][0];
    CHECK(edit0["newText"] == "he");
    CHECK(edit0["range"] == json{{"start", pos(0, 8)}, {"end", pos(0, 9)}});
    CHECK(actions[1]["title"] == "Aplicar todas en este párrafo");
    CHECK(actions[1]["edit"]["changes"][uri].size() == 2);

    // A range in the code paragraph: nothing.
    s.send(request(3, "textDocument/codeAction", code_action_params(uri, pos(3, 0), pos(3, 5))));
    all = s.all();
    CHECK(response(all, 3)->at("result") == json::array());

    // A new version: diagnostics for it.
    s.send(notification("textDocument/didChange",
                        {{"textDocument", {{"uri", uri}, {"version", 4}}},
                         {"contentChanges", json::array({{{"text", "Bien.\n" + std::string(kWrong)}}})}}));
    all = s.all();
    const std::vector<json> after = notifications(all, "textDocument/publishDiagnostics");
    REQUIRE(after.size() == 2);
    CHECK(after[1]["params"]["version"] == 4);
    CHECK(after[1]["params"]["diagnostics"][0]["range"]["start"] == pos(1, 8));

    // Unknown request, close, shutdown, exit.
    s.send(request(5, "textDocument/hover", json::object()));
    s.send(notification("$/cancelRequest", {{"id", 5}}));
    s.send(notification("textDocument/didClose", {{"textDocument", {{"uri", uri}}}}));
    s.send(request(6, "shutdown", nullptr));
    CHECK_FALSE(s.send(notification("exit", nullptr)));
    CHECK(s.server->exit_code() == 0);
    all = s.all();
    CHECK(response(all, 5)->at("error")["code"] == -32601);
    CHECK(response(all, 6)->at("result").is_null());
    const std::vector<json> closed = notifications(all, "textDocument/publishDiagnostics");
    CHECK(closed.back()["params"]["diagnostics"] == json::array());
}

TEST_CASE("lsp session: UTF-8 and UTF-32 when the client does not offer UTF-16") {
    for (const auto& [name, start] : {std::pair{"utf-8", 12}, std::pair{"utf-32", 6}}) {
        Session s;
        s.send(request(1, "initialize", initialize_params(json::array({name}))));
        s.send(notification("textDocument/didOpen", open_params("file:///c", "gitcommit", kWrong)));
        const std::vector<json> all = s.all();
        CHECK(response(all, 1)->at("result")["capabilities"]["positionEncoding"] == name);
        const std::vector<json> diags = notifications(all, "textDocument/publishDiagnostics");
        REQUIRE(diags.size() == 1);
        CHECK(diags[0]["params"]["diagnostics"][0]["range"]["start"] == pos(0, static_cast<std::size_t>(start)));
    }
}

TEST_CASE("lsp session: engine errors, malformed input and exit without shutdown") {
    Session s;
    s.engine.error = "load_failed";
    CHECK(s.send(notification("textDocument/didOpen", open_params("file:///x", "plaintext", kWrong))));
    s.send(request(1, "textDocument/codeAction", code_action_params("file:///x", pos(0, 0), pos(0, 0))));
    CHECK(s.server->handle("{not json"));
    CHECK(s.server->handle("[]"));
    s.send(request(2, "initialize", initialize_params(nullptr)));
    s.send(notification("textDocument/didOpen", open_params("file:///x", "plaintext", kWrong)));
    s.send(notification("textDocument/didOpen", open_params("file:///y", "plaintext", kWrong)));
    s.send(request(3, "textDocument/codeAction", {{"bad", 1}}));
    const std::vector<json> all = s.all();
    CHECK(response(all, 1)->at("error")["code"] == -32002);
    CHECK(response(all, 2)->at("result")["capabilities"]["positionEncoding"] == "utf-16");
    CHECK(response(all, 3)->at("error")["code"] == -32602);
    CHECK(notifications(all, "textDocument/publishDiagnostics").empty());
    const std::vector<json> shown = notifications(all, "window/showMessage");
    REQUIRE(shown.size() == 1);
    CHECK(shown[0]["params"]["type"] == 1);
    CHECK(shown[0]["params"]["message"].get<std::string>().find("modelo") != std::string::npos);
    CHECK_FALSE(s.send(notification("exit", nullptr)));
    CHECK(s.server->exit_code() == 1);
}

TEST_CASE("lsp run: a whole session over streams") {
    ScriptEngine engine;
    engine.outputs[kWrong] = kRight;
    const std::string uri = "file:///m.txt";
    std::istringstream in(frame(request(1, "initialize", initialize_params(nullptr))) +
                          frame(notification("textDocument/didOpen", open_params(uri, "plaintext", kWrong))) +
                          frame(request(2, "shutdown", nullptr)) + frame(notification("exit", nullptr)));
    std::ostringstream out;
    revisa::lsp::Options options;  // the real debounce: shutdown flushes it
    CHECK(revisa::lsp::run(in, out, engine, options) == 0);
    const std::vector<json> all = messages(out.str());
    REQUIRE(all.size() == 3);
    CHECK(all[1]["method"] == "textDocument/publishDiagnostics");
    CHECK(all[1]["params"]["diagnostics"].size() == 2);
    CHECK(all[2]["id"] == 2);
}
