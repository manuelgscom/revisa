#pragma once
// Pure helpers of the popup: the diff of the whole text as rich text, the label and title of one
// edit, the human name of each type, the text to paste for the edits the user keeps, and the
// length limit. No widgets: unit tested.

#include <QString>

#include <cstddef>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/check/check.hpp"

namespace revisa::tray {

// The same limit as `revisa serve` (413 too_long), in Unicode code points.
inline constexpr std::size_t kMaxTextChars = 20000;

// Code points of text (a surrogate pair counts once).
std::size_t codePoints(const QString& text);

// Colors of the diff, as CSS colors ("#rrggbb"). The defaults are the light theme's
// (theme::colors(Scheme::Light)); the popup passes the ones of the theme in use.
struct DiffColors {
    QString delFg = QStringLiteral("#b42318");
    QString delBg = QStringLiteral("#ffebe9");
    QString insFg = QStringLiteral("#116329");
    QString insBg = QStringLiteral("#dafbe1");
};

// The whole original text as HTML for a QLabel, in one block with a comfortable line height: for
// each enabled edit, the removed text struck through in red on a light red background and the
// inserted text in semibold green on a light green background, with a thin space between both
// halves of a replacement; disabled edits and everything else as the original text,
// HTML-escaped, with newlines as <br>. Edit ranges are the UTF-16 offsets of check::Edit
// (start/end) over `original`; an edit that overlaps the previous one or falls outside the text
// is shown as unchanged text. enabled[i] belongs to edits[i]; missing = enabled.
QString diffHtml(const QString& original, const std::vector<check::Edit>& edits,
                 const std::vector<bool>& enabled, const DiffColors& colors = {});

// «from» → «to» for the edit's checkbox (accessible name, tests). An empty side is (nada);
// whitespace-only sides show each space as ␣ so a removed double space is visible.
QString editLabel(const check::Edit& edit);

// The title of the edit's row as HTML: the old text struck through in `muted`, an arrow, the new
// text semibold in `strong`. An insertion reads «Añadir X», a deletion «Quitar X»; whitespace
// shows as ␣.
QString editTitleHtml(const check::Edit& edit, const QString& muted, const QString& strong);

// Short Spanish name of a type for the chip and the settings («Tilde», «Haber / a ver»…).
QString typeLabel(catalog::Type type);

// «1 corrección», «3 correcciones».
QString correctionsCount(std::size_t n);

// original with the enabled edits applied (check::apply over the UTF-8 bytes).
QString applyEnabled(const QString& original, const std::vector<check::Edit>& edits,
                     const std::vector<bool>& enabled);

}  // namespace revisa::tray
