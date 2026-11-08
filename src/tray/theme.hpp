#pragma once
// Look of revisa-tray: one palette, one stylesheet and a few fonts for every window of ours.
//
//   install(app) ─► Fusion (through a small proxy style: modern check boxes)
//                ─► QPalette for the scheme (light / dark, following the desktop)
//                ─► app font, at least 10 pt
//   our top-level widgets ─► setStyleSheet(styleSheet(colors()))  (selectors scoped to our names)
//                         ─► notifier()->changed() when the desktop switches light <-> dark
//
// The scheme comes from QStyleHints::colorScheme(); when the platform does not know it, from the
// lightness of the platform palette's Window color. REVISA_TRAY_THEME=light|dark forces it.

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPalette>
#include <QString>

class QApplication;
class QPainter;
class QRectF;

namespace revisa::tray::theme {

enum class Scheme { Light, Dark };

struct Colors {
    QColor window;         // dialog background
    QColor card;           // popup, toast, settings sections
    QColor panel;          // soft panel inside a card (the diff)
    QColor border;         // card and section borders, dividers
    QColor borderStrong;   // inputs, unchecked check boxes
    QColor text;           // primary text
    QColor textSecondary;  // explanations, captions (>= 4.5:1 on card)
    QColor textTertiary;   // disabled / unchecked rows
    QColor hover;          // row and ghost button hover
    QColor pressed;
    QColor accent;         // focus ring, check box fill, links
    QColor buttonFill;     // primary button (white text on it, >= 4.5:1)
    QColor buttonHover;
    QColor buttonPressed;
    QColor onAccent;       // text and icons on accent / buttonFill
    QColor delFg, delBg;   // removed text in the diff
    QColor insFg, insBg;   // inserted text in the diff
    QColor chipBg, chipFg;
    QColor keycapBg, keycapBorder;
    QColor success, warning, danger;
    QColor scrollHandle;
    QColor shadow;  // drop shadow of floating cards (with alpha)
};

// The colors of a scheme (constant tables).
const Colors& colors(Scheme scheme);
// The scheme in use (the last one applied; Light before install()).
Scheme scheme();
// The colors in use.
const Colors& colors();

// What the desktop asks for: REVISA_TRAY_THEME, else QStyleHints::colorScheme(), else the
// lightness of `platformWindow` (the platform palette's Window color).
Scheme detectScheme(const QColor& platformWindow);

QPalette palette(const Colors& c);
// The stylesheet for our top-level widgets (popup, toast, settings, menu, about box). Every
// selector is scoped by object name or class + property, so foreign dialogs are untouched.
QString styleSheet(const Colors& c);

// "#rrggbb", or "rgba(r,g,b,a)" when the color is not opaque.
QString css(const QColor& color);

// WCAG contrast ratio between two opaque colors (1..21).
double contrast(const QColor& a, const QColor& b);

// revisa's check box: a rounded square, accent-filled with a white check when on (the proxy style
// and the popup's rows draw it with this).
void paintCheckBox(QPainter& p, const QRectF& box, bool on, bool partial, bool enabled);

// Fonts derived from the application font.
QFont baseFont();
QFont smallFont();     // captions, chips, keycaps: base - 1 pt (>= 9 pt)
QFont strongFont();    // base, semibold

// Sets Fusion (through revisa's proxy style), the palette of detectScheme() and the app font
// (>= 10 pt), and follows the desktop when it switches between light and dark.
void install(QApplication& app);
// Applies `s` now: palette, then notifier()->changed(). Used by install() and the tests.
void apply(Scheme s);

class Notifier : public QObject {
    Q_OBJECT
signals:
    void changed();
};
Notifier* notifier();

}  // namespace revisa::tray::theme
