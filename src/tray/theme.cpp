#include "tray/theme.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>

#include <algorithm>
#include <cmath>

#include "tray/icons.hpp"

namespace revisa::tray::theme {

namespace {

QColor rgb(const char* hex) { return QColor(QLatin1String(hex)); }

Colors makeLight() {
    Colors c;
    c.window = rgb("#f4f5f7");
    c.card = rgb("#ffffff");
    c.panel = rgb("#f6f8fa");
    c.border = rgb("#e1e4e8");
    c.borderStrong = rgb("#d0d7de");
    c.text = rgb("#1f2328");
    c.textSecondary = rgb("#59636e");
    c.textTertiary = rgb("#8c959f");
    c.hover = rgb("#f1f3f5");
    c.pressed = rgb("#e7eaee");
    c.accent = rgb("#1f6feb");
    c.buttonFill = rgb("#1f6feb");
    c.buttonHover = rgb("#1a63d6");
    c.buttonPressed = rgb("#1557c0");
    c.onAccent = rgb("#ffffff");
    c.delFg = rgb("#b42318");
    c.delBg = rgb("#ffebe9");
    c.insFg = rgb("#116329");
    c.insBg = rgb("#dafbe1");
    c.chipBg = rgb("#eef1f4");
    c.chipFg = rgb("#424a53");
    c.keycapBg = rgb("#f6f8fa");
    c.keycapBorder = rgb("#d0d7de");
    c.success = rgb("#1a7f37");
    c.warning = rgb("#9a6700");
    c.danger = rgb("#cf222e");
    c.scrollHandle = rgb("#c9d0d7");
    c.shadow = QColor(15, 23, 42, 52);
    return c;
}

Colors makeDark() {
    Colors c;
    c.window = rgb("#16191d");
    c.card = rgb("#1f2329");
    c.panel = rgb("#262b32");
    c.border = rgb("#30363d");
    c.borderStrong = rgb("#3d444d");
    c.text = rgb("#e6edf3");
    c.textSecondary = rgb("#9da7b3");
    c.textTertiary = rgb("#6e7681");
    c.hover = rgb("#2a3038");
    c.pressed = rgb("#323943");
    c.accent = rgb("#4493f8");
    c.buttonFill = rgb("#1f6feb");
    c.buttonHover = rgb("#2a74ec");
    c.buttonPressed = rgb("#1a63d6");
    c.onAccent = rgb("#ffffff");
    c.delFg = rgb("#ffa198");
    c.delBg = rgb("#3d2327");
    c.insFg = rgb("#7ee787");
    c.insBg = rgb("#1d3a29");
    c.chipBg = rgb("#2d333b");
    c.chipFg = rgb("#c9d1d9");
    c.keycapBg = rgb("#262b32");
    c.keycapBorder = rgb("#3d444d");
    c.success = rgb("#3fb950");
    c.warning = rgb("#d29922");
    c.danger = rgb("#f85149");
    c.scrollHandle = rgb("#484f58");
    c.shadow = QColor(0, 0, 0, 150);
    return c;
}

Scheme g_scheme = Scheme::Light;
QColor g_platformWindow;  // the platform palette's Window color, read before we replace it

// Fusion with revisa's check boxes: a rounded square, accent-filled with a white check when on.
// Everything else is Fusion with our palette.
class Style : public QProxyStyle {
public:
    Style() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
        if (element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck) {
            const qreal side = std::min(option->rect.width(), option->rect.height());
            QRectF box(0, 0, side, side);
            box.moveCenter(QRectF(option->rect).center());
            paintCheckBox(*painter, box, option->state.testFlag(State_On), option->state.testFlag(State_NoChange),
                          option->state.testFlag(State_Enabled));
            return;
        }
        // Item views draw their own hover/selection; no dotted focus rectangles.
        if (element == PE_FrameFocusRect && qobject_cast<const QAbstractItemView*>(widget)) return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override {
        switch (metric) {
            case PM_IndicatorWidth:
            case PM_IndicatorHeight:
                return 18;
            case PM_CheckBoxLabelSpacing:
                return 10;
            default:
                return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

double luminance(const QColor& color) {
    auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

}  // namespace

const Colors& colors(Scheme scheme) {
    static const Colors light = makeLight();
    static const Colors dark = makeDark();
    return scheme == Scheme::Dark ? dark : light;
}

Scheme scheme() { return g_scheme; }

const Colors& colors() { return colors(g_scheme); }

Scheme detectScheme(const QColor& platformWindow) {
    const QString forced = qEnvironmentVariable("REVISA_TRAY_THEME").toLower();
    if (forced == QLatin1String("dark")) return Scheme::Dark;
    if (forced == QLatin1String("light")) return Scheme::Light;
    if (qGuiApp) {
        switch (QGuiApplication::styleHints()->colorScheme()) {
            case Qt::ColorScheme::Dark: return Scheme::Dark;
            case Qt::ColorScheme::Light: return Scheme::Light;
            case Qt::ColorScheme::Unknown: break;
        }
    }
    if (platformWindow.isValid() && platformWindow.lightnessF() < 0.5) return Scheme::Dark;
    return Scheme::Light;
}

void paintCheckBox(QPainter& p, const QRectF& rect, bool on, bool partial, bool enabled) {
    const Colors& c = colors();
    const qreal side = std::min(rect.width(), rect.height());
    QRectF box(0, 0, side, side);
    box.moveCenter(rect.center());
    box.adjust(0.75, 0.75, -0.75, -0.75);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (on || partial) {
        p.setPen(Qt::NoPen);
        p.setBrush(enabled ? c.accent : c.textTertiary);
        p.drawRoundedRect(box, 4.5, 4.5);
        if (partial) {
            p.setPen(QPen(c.onAccent, 2.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(box.left() + side * 0.28, box.center().y()),
                       QPointF(box.right() - side * 0.28, box.center().y()));
        } else {
            icons::paint(p, icons::Glyph::Check, box.adjusted(1.5, 1.5, -1.5, -1.5), c.onAccent, 2.0);
        }
    } else {
        p.setPen(QPen(enabled ? c.textTertiary : c.borderStrong, 1.5));
        p.setBrush(c.card);
        p.drawRoundedRect(box, 4.5, 4.5);
    }
    p.restore();
}

QPalette palette(const Colors& c) {
    QPalette p;
    for (const QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool off = g == QPalette::Disabled;
        p.setColor(g, QPalette::Window, c.window);
        p.setColor(g, QPalette::WindowText, off ? c.textTertiary : c.text);
        p.setColor(g, QPalette::Base, c.card);
        p.setColor(g, QPalette::AlternateBase, c.panel);
        p.setColor(g, QPalette::Text, off ? c.textTertiary : c.text);
        p.setColor(g, QPalette::PlaceholderText, c.textTertiary);
        p.setColor(g, QPalette::Button, c.card);
        p.setColor(g, QPalette::ButtonText, off ? c.textTertiary : c.text);
        p.setColor(g, QPalette::BrightText, c.onAccent);
        p.setColor(g, QPalette::Highlight, off ? c.textTertiary : c.accent);
        p.setColor(g, QPalette::HighlightedText, c.onAccent);
        p.setColor(g, QPalette::Accent, c.accent);
        p.setColor(g, QPalette::Link, c.accent);
        p.setColor(g, QPalette::LinkVisited, c.accent);
        p.setColor(g, QPalette::ToolTipBase, c.card);
        p.setColor(g, QPalette::ToolTipText, c.text);
        p.setColor(g, QPalette::Light, c.card);
        p.setColor(g, QPalette::Midlight, c.border);
        p.setColor(g, QPalette::Mid, c.borderStrong);
        p.setColor(g, QPalette::Dark, c.textTertiary);
        p.setColor(g, QPalette::Shadow, c.shadow);
    }
    return p;
}

QString css(const QColor& color) {
    if (color.alpha() == 255) return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QString styleSheet(const Colors& c) {
    // %-placeholders would be unreadable here: a tiny named substitution instead.
    QString s = QStringLiteral(R"(
/* Floating cards: popup and toast */
QFrame#card { background: @card; border: 1px solid @border; border-radius: 12px; }
QFrame#card[floating="false"] { border-radius: 0px; border-color: @borderStrong; }
QFrame#diffPanel { background: @panel; border: none; border-radius: 8px; }
QFrame#divider { background: @border; border: none; max-height: 1px; min-height: 1px; }
QLabel { color: @text; background: transparent; }
QLabel[role="secondary"] { color: @textSecondary; }
QLabel[role="title"] { color: @text; }
QLabel#chip { background: @chipBg; color: @chipFg; border-radius: 6px; padding: 1px 7px; }
QLabel#chip[off="true"] { color: @textTertiary; }
QLabel#keycap { background: @keycapBg; color: @textSecondary; border: 1px solid @keycapBorder;
                border-bottom-width: 2px; border-radius: 5px; padding: 0px 5px; }
QToolButton#close { background: transparent; border: none; border-radius: 14px; }
QToolButton#close:hover { background: @hover; }
QToolButton#close:pressed { background: @pressed; }

/* Buttons */
QPushButton { background: @card; color: @text; border: 1px solid @borderStrong; border-radius: 8px;
              padding: 0px 14px; min-height: 30px; }
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @pressed; }
QPushButton:focus { border: 2px solid @accent; padding: 0px 13px; }
QPushButton[variant="primary"] { background: @buttonFill; color: @onAccent; border: 1px solid @buttonFill; }
QPushButton[variant="primary"]:hover { background: @buttonHover; border-color: @buttonHover; }
QPushButton[variant="primary"]:pressed { background: @buttonPressed; border-color: @buttonPressed; }
QPushButton[variant="primary"]:focus { border: 2px solid @accent; }
QPushButton[variant="ghost"] { background: transparent; color: @textSecondary; border: 1px solid transparent; }
QPushButton[variant="ghost"]:hover { background: @hover; color: @text; }
QPushButton[variant="ghost"]:pressed { background: @pressed; color: @text; }
QToolButton[variant="stepper"] { background: @card; border: 1px solid @borderStrong; border-radius: 8px; }
QToolButton[variant="stepper"]:hover { background: @hover; }
QToolButton[variant="stepper"]:pressed { background: @pressed; }
QToolButton[variant="stepper"]:disabled { background: @panel; border-color: @border; }

/* Scroll bars: thin, no arrows */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget#scrollContent { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px 4px 2px; }
QScrollBar::handle:vertical { background: @scrollHandle; border-radius: 3px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

/* Settings */
QDialog#revisa-settings { background: @window; }
QFrame#section { background: @card; border: 1px solid @border; border-radius: 10px; }
QLabel#sectionTitle { color: @textSecondary; }
QKeySequenceEdit QLineEdit, QSpinBox { background: @card; color: @text; border: 1px solid @borderStrong;
              border-radius: 8px; padding: 0px 10px; min-height: 30px; selection-background-color: @accent; }
QKeySequenceEdit QLineEdit:focus, QSpinBox:focus { border: 2px solid @accent; padding: 0px 9px; }
QListWidget#types { background: transparent; border: none; outline: none; }
QListWidget#types::item { color: @text; border-radius: 6px; padding: 4px 4px; }
QListWidget#types::item:hover { background: @hover; }
QListWidget#types::item:selected { background: transparent; color: @text; }
QListWidget#types::item:selected:hover { background: @hover; }
QCheckBox { color: @text; spacing: 10px; }

/* Tray menu */
QMenu#revisa-menu { background: @card; border: 1px solid @borderStrong; padding: 6px; }
QMenu#revisa-menu::item { color: @text; padding: 6px 28px 6px 10px; border-radius: 6px; background: transparent; }
QMenu#revisa-menu::item:selected { background: @hover; }
QMenu#revisa-menu::item:disabled { color: @textSecondary; }
QMenu#revisa-menu::icon { padding-left: 10px; }
QMenu#revisa-menu::separator { height: 1px; background: @border; margin: 5px 8px; }
)");
    const std::pair<const char*, const QColor*> vars[] = {
        {"@window", &c.window},
        {"@card", &c.card},
        {"@panel", &c.panel},
        {"@borderStrong", &c.borderStrong},
        {"@border", &c.border},
        {"@textSecondary", &c.textSecondary},
        {"@textTertiary", &c.textTertiary},
        {"@text", &c.text},
        {"@hover", &c.hover},
        {"@pressed", &c.pressed},
        {"@accent", &c.accent},
        {"@buttonFill", &c.buttonFill},
        {"@buttonHover", &c.buttonHover},
        {"@buttonPressed", &c.buttonPressed},
        {"@onAccent", &c.onAccent},
        {"@chipBg", &c.chipBg},
        {"@chipFg", &c.chipFg},
        {"@keycapBg", &c.keycapBg},
        {"@keycapBorder", &c.keycapBorder},
        {"@scrollHandle", &c.scrollHandle},
    };
    // Longest names first where one is a prefix of another (@borderStrong before @border, @text…).
    for (const auto& [name, color] : vars) s.replace(QLatin1String(name), css(*color));
    return s;
}

QFont baseFont() {
    QFont f = QApplication::font();
    if (f.pointSizeF() > 0 && f.pointSizeF() < 10) f.setPointSizeF(10);
    return f;
}

QFont smallFont() {
    QFont f = baseFont();
    if (f.pointSizeF() > 0) f.setPointSizeF(std::max(9.0, f.pointSizeF() - 1));
    return f;
}

QFont strongFont() {
    QFont f = baseFont();
    f.setWeight(QFont::DemiBold);
    return f;
}

Notifier* notifier() {
    static QPointer<Notifier> n;
    if (!n) n = new Notifier();  // lives until exit (owned by nobody; tiny)
    return n;
}

void apply(Scheme s) {
    g_scheme = s;
    if (qApp) QApplication::setPalette(palette(colors(s)));
    emit notifier()->changed();
}

void install(QApplication& app) {
    g_platformWindow = QApplication::palette().color(QPalette::Active, QPalette::Window);
    QApplication::setStyle(new Style());
    QApplication::setFont(baseFont());
    apply(detectScheme(g_platformWindow));
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [] {
        const Scheme s = detectScheme(g_platformWindow);
        if (s != g_scheme) apply(s);
    });
}

}  // namespace revisa::tray::theme
