// The popup's pure helpers (src/tray/diff_html.*): diff HTML, edit labels, the text to paste and
// the length limit.

#include "doctest.h"

#include "tray/diff_html.hpp"

#include <QString>

#include <set>
#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/check/check.hpp"

using namespace revisa;
using revisa::tray::applyEnabled;
using revisa::tray::codePoints;
using revisa::tray::diffHtml;
using revisa::tray::correctionsCount;
using revisa::tray::DiffColors;
using revisa::tray::editLabel;
using revisa::tray::editTitleHtml;
using revisa::tray::typeLabel;

namespace {

const QString kOpen = QStringLiteral("<div style=\"line-height:145%\"><span style=\"white-space:pre-wrap\">");
const QString kDel =
    QStringLiteral("<span style=\"color:#b42318;background-color:#ffebe9;text-decoration:line-through\">");
const QString kIns = QStringLiteral("<span style=\"color:#116329;background-color:#dafbe1;font-weight:600\">");
const QString kEnd = QStringLiteral("</span>");
const QString kTail = QStringLiteral("</span></div>");
const QString kGap = QStringLiteral("&#8201;");

// An edit over `text` from its UTF-16 range, with the byte range computed as check_text does.
check::Edit edit(const QString& text, qsizetype start, qsizetype end, const QString& to) {
    check::Edit e;
    e.start = static_cast<std::size_t>(start);
    e.end = static_cast<std::size_t>(end);
    e.byte_start = static_cast<std::size_t>(text.left(start).toUtf8().size());
    e.byte_end = static_cast<std::size_t>(text.left(end).toUtf8().size());
    e.from = text.mid(start, end - start).toStdString();
    e.to = to.toStdString();
    return e;
}

}  // namespace

TEST_CASE("diffHtml: no edits is the escaped text") {
    CHECK(diffHtml(QStringLiteral("Hola"), {}, {}) == kOpen + QStringLiteral("Hola") + kTail);
    CHECK(diffHtml(QString(), {}, {}) == kOpen + kTail);
}

TEST_CASE("diffHtml: replace, insert and delete") {
    const QString t = QStringLiteral("e echo la tarea");
    // replace
    CHECK(diffHtml(t, {edit(t, 0, 6, QStringLiteral("he hecho"))}, {}) ==
          kOpen + kDel + QStringLiteral("e echo") + kEnd + kGap + kIns + QStringLiteral("he hecho") + kEnd +
              QStringLiteral(" la tarea") + kTail);
    // insert (empty range)
    const QString q = QStringLiteral("Qué tal?");
    CHECK(diffHtml(q, {edit(q, 0, 0, QStringLiteral("¿"))}, {}) ==
          kOpen + kIns + QStringLiteral("¿") + kEnd + q + kTail);
    // delete
    const QString d = QStringLiteral("Hola  Ana");
    CHECK(diffHtml(d, {edit(d, 4, 5, QString())}, {}) ==
          kOpen + QStringLiteral("Hola") + kDel + QStringLiteral(" ") + kEnd + QStringLiteral(" Ana") + kTail);
}

TEST_CASE("diffHtml: an unchecked edit shows the original text") {
    const QString t = QStringLiteral("haber si e echo");
    const std::vector<check::Edit> edits = {edit(t, 0, 5, QStringLiteral("a ver")),
                                            edit(t, 9, 15, QStringLiteral("he hecho"))};
    CHECK(diffHtml(t, edits, {false, true}) ==
          kOpen + QStringLiteral("haber si ") + kDel + QStringLiteral("e echo") + kEnd + kGap + kIns +
              QStringLiteral("he hecho") + kEnd + kTail);
    CHECK(diffHtml(t, edits, {false, false}) == kOpen + t + kTail);
}

TEST_CASE("diffHtml: UTF-16 offsets with ñ, ¿ and characters outside the BMP") {
    const QString t = QStringLiteral("😀 ¿Cómo esta el niño?");
    const qsizetype at = t.indexOf(QStringLiteral("esta"));
    REQUIRE(at == 9);  // the emoji is a surrogate pair (2 UTF-16 units)
    const std::vector<check::Edit> edits = {edit(t, at, at + 4, QStringLiteral("está"))};
    CHECK(diffHtml(t, edits, {}) == kOpen + QStringLiteral("😀 ¿Cómo ") + kDel + QStringLiteral("esta") + kEnd +
                                        kGap + kIns + QStringLiteral("está") + kEnd + QStringLiteral(" el niño?") + kTail);
    CHECK(applyEnabled(t, edits, {true}) == QStringLiteral("😀 ¿Cómo está el niño?"));
}

TEST_CASE("diffHtml: escapes HTML and turns newlines into <br>") {
    const QString t = QStringLiteral("a<b & c>\nd");
    CHECK(diffHtml(t, {}, {}) == kOpen + QStringLiteral("a&lt;b &amp; c&gt;<br>d") + kTail);
    const std::vector<check::Edit> edits = {edit(t, 0, 2, QStringLiteral("<x>"))};
    CHECK(diffHtml(t, edits, {}) == kOpen + kDel + QStringLiteral("a&lt;") + kEnd + kGap + kIns +
                                        QStringLiteral("&lt;x&gt;") + kEnd +
                                        QStringLiteral("b &amp; c&gt;<br>d") + kTail);
}

TEST_CASE("diffHtml: overlapping or out-of-range edits are shown as plain text") {
    const QString t = QStringLiteral("abcdef");
    const std::vector<check::Edit> edits = {edit(t, 1, 3, QStringLiteral("X")), edit(t, 2, 4, QStringLiteral("Y"))};
    CHECK(diffHtml(t, edits, {}) ==
          kOpen + QStringLiteral("a") + kDel + QStringLiteral("bc") + kEnd + kGap + kIns + QStringLiteral("X") + kEnd +
              QStringLiteral("def") + kTail);
    check::Edit outside;
    outside.start = 4;
    outside.end = 99;
    CHECK(diffHtml(t, {outside}, {}) == kOpen + t + kTail);
}

TEST_CASE("applyEnabled: the text to paste follows the checkboxes") {
    const QString t = QStringLiteral("Hola Ana, haber si mañana e echo la tarea.");
    const qsizetype h = t.indexOf(QStringLiteral("haber"));
    const qsizetype e = t.indexOf(QStringLiteral("e echo"));
    const std::vector<check::Edit> edits = {edit(t, h, h + 5, QStringLiteral("a ver")),
                                            edit(t, e, e + 6, QStringLiteral("he hecho"))};
    CHECK(applyEnabled(t, edits, {true, true}) == QStringLiteral("Hola Ana, a ver si mañana he hecho la tarea."));
    CHECK(applyEnabled(t, edits, {false, true}) == QStringLiteral("Hola Ana, haber si mañana he hecho la tarea."));
    CHECK(applyEnabled(t, edits, {true, false}) == QStringLiteral("Hola Ana, a ver si mañana e echo la tarea."));
    CHECK(applyEnabled(t, edits, {false, false}) == t);
    CHECK(applyEnabled(t, edits, {}) == QStringLiteral("Hola Ana, a ver si mañana he hecho la tarea."));
}

TEST_CASE("editLabel") {
    const QString t = QStringLiteral("haber  ,");
    CHECK(editLabel(edit(t, 0, 5, QStringLiteral("a ver"))) == QStringLiteral("«haber» → «a ver»"));
    CHECK(editLabel(edit(t, 5, 7, QStringLiteral(" "))) == QStringLiteral("«␣␣» → «␣»"));
    CHECK(editLabel(edit(t, 7, 8, QString())) == QStringLiteral("«,» → (nada)"));
    CHECK(editLabel(edit(t, 0, 0, QStringLiteral("¿"))) == QStringLiteral("(nada) → «¿»"));
}

TEST_CASE("codePoints counts a surrogate pair once") {
    CHECK(codePoints(QString()) == 0);
    CHECK(codePoints(QStringLiteral("ñandú")) == 5);
    CHECK(codePoints(QStringLiteral("😀a")) == 2);
    CHECK(revisa::tray::kMaxTextChars == 20000);
}

TEST_CASE("diffHtml: colors come from the theme") {
    const QString t = QStringLiteral("e echo");
    DiffColors dark;
    dark.delFg = QStringLiteral("#ffa198");
    dark.delBg = QStringLiteral("#3d2327");
    dark.insFg = QStringLiteral("#7ee787");
    dark.insBg = QStringLiteral("#1d3a29");
    const QString html = diffHtml(t, {edit(t, 0, 6, QStringLiteral("he hecho"))}, {}, dark);
    CHECK(html.contains(QStringLiteral("color:#ffa198;background-color:#3d2327;text-decoration:line-through")));
    CHECK(html.contains(QStringLiteral("color:#7ee787;background-color:#1d3a29;font-weight:600\">he hecho<")));
}

TEST_CASE("editTitleHtml: replace, insert, delete and whitespace") {
    const QString t = QStringLiteral("despues  ,");
    const QString m = QStringLiteral("#888888");
    const QString s = QStringLiteral("#111111");
    CHECK(editTitleHtml(edit(t, 0, 7, QStringLiteral("después")), m, s) ==
          QStringLiteral("<span style=\"color:#888888;text-decoration:line-through\">despues</span>"
                         "<span style=\"color:#888888\">&nbsp;&nbsp;→&nbsp;&nbsp;</span>"
                         "<span style=\"color:#111111;font-weight:600\">después</span>"));
    CHECK(editTitleHtml(edit(t, 0, 0, QStringLiteral("¿")), m, s) ==
          QStringLiteral("<span style=\"color:#888888\">Añadir&nbsp;</span>"
                         "<span style=\"color:#111111;font-weight:600\">¿</span>"));
    CHECK(editTitleHtml(edit(t, 9, 10, QString()), m, s) ==
          QStringLiteral("<span style=\"color:#888888\">Quitar&nbsp;</span>"
                         "<span style=\"color:#888888;text-decoration:line-through\">,</span>"));
    CHECK(editTitleHtml(edit(t, 7, 9, QStringLiteral(" ")), m, s).contains(QStringLiteral(">␣␣<")));
    CHECK(editTitleHtml(edit(QStringLiteral("a<b"), 0, 3, QStringLiteral("a&b")), m, s).contains(QStringLiteral("a&amp;b")));
}

TEST_CASE("typeLabel: every type has a Spanish name, all different") {
    std::set<QString> seen;
    for (std::size_t i = 0; i < catalog::kTypeCount; ++i) {
        const QString label = typeLabel(static_cast<catalog::Type>(i));
        INFO(i);
        CHECK_FALSE(label.isEmpty());
        CHECK(label.size() <= 20);  // fits a chip
        CHECK(seen.insert(label).second);
    }
    CHECK(typeLabel(catalog::Type::TildeFalta) == QStringLiteral("Falta tilde"));
    CHECK(typeLabel(catalog::Type::HaberAVer) == QStringLiteral("Haber / a ver"));
}

TEST_CASE("correctionsCount") {
    CHECK(correctionsCount(1) == QStringLiteral("1 corrección"));
    CHECK(correctionsCount(3) == QStringLiteral("3 correcciones"));
}
