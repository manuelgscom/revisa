// The look of revisa-tray (src/tray/theme.*, icons.*, card_window.*, popup.*, toast.*): colors
// and contrast per scheme, painted icons, and the popup, toast and settings rendered offscreen in
// light and dark without Qt warnings.
//
// Render harness (not part of the app): with REVISA_RENDER_DIR set, the renders are also saved
// there as PNG for a visual review, the floating cards composited on a neutral background so the
// shadow shows. Run it at DPR 1 and 2:
//   REVISA_RENDER_DIR=/tmp/r QT_QPA_PLATFORM=offscreen build-gui/revisa_tray_tests -tc='look: render*'
//   REVISA_RENDER_DIR=/tmp/r QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 build-gui/revisa_tray_tests -tc='look: render*'

#include "doctest.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QStyleHints>

#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/check/check.hpp"
#include "tray/diff_html.hpp"
#include "tray/icons.hpp"
#include "tray/popup.hpp"
#include "tray/settings_dialog.hpp"
#include "tray/theme.hpp"
#include "tray/toast.hpp"

using namespace revisa;
using namespace revisa::tray;

// Outside any namespace: Q_INIT_RESOURCE declares the resource's init function where it is used.
static void initTrayResources() { Q_INIT_RESOURCE(tray); }

namespace {

void ensureApp() {
    if (QCoreApplication::instance()) return;
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static int argc = 1;
    static char arg0[] = "revisa_tray_tests";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

void ensureTheme() {
    ensureApp();
    static bool installed = false;
    if (installed) return;
    installed = true;
    initTrayResources();
    theme::install(*qApp);
}

// Collects Qt warnings for the scope. The offscreen plugin's own complaints about what it does
// not implement are not ours.
QStringList* g_warnings = nullptr;
QtMessageHandler g_previous = nullptr;
void collect(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (g_warnings && type >= QtWarningMsg && !msg.contains(QLatin1String("This plugin does not support")))
        g_warnings->append(msg);
    if (g_previous) g_previous(type, ctx, msg);
}
class WarningTrap {
public:
    WarningTrap() {
        g_warnings = &list_;
        g_previous = qInstallMessageHandler(collect);
    }
    ~WarningTrap() {
        qInstallMessageHandler(g_previous);
        g_warnings = nullptr;
    }
    const QStringList& list() const { return list_; }
    WarningTrap(const WarningTrap&) = delete;
    WarningTrap& operator=(const WarningTrap&) = delete;

private:
    QStringList list_;
};

void settle() {
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// Non-transparent pixels of an image.
int inked(const QImage& image) {
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 16) ++n;
    return n;
}

check::Edit edit(const QString& text, const QString& from, const QString& to, catalog::Type type, const QString& why) {
    const qsizetype start = text.indexOf(from);
    REQUIRE(start >= 0);
    const qsizetype end = start + from.size();
    check::Edit e;
    e.start = static_cast<std::size_t>(start);
    e.end = static_cast<std::size_t>(end);
    e.byte_start = static_cast<std::size_t>(text.left(start).toUtf8().size());
    e.byte_end = static_cast<std::size_t>(text.left(end).toUtf8().size());
    e.from = from.toStdString();
    e.to = to.toStdString();
    e.type = type;
    e.explanation = why.toStdString();
    return e;
}

const QString kText = QStringLiteral("Hola Ana, ya e echo la compra y despues paso por tu casa. ¿Quedamos el lunes?");

std::vector<check::Edit> sampleEdits() {
    return {
        edit(kText, QStringLiteral("e "), QStringLiteral("he "), catalog::Type::VerboHaberH,
             QStringLiteral("Aquí va «he», del verbo «haber» (como en «he venido» o «ha dicho»).")),
        edit(kText, QStringLiteral("echo"), QStringLiteral("hecho"), catalog::Type::EchoHecho,
             QStringLiteral("«hecho» es de «hacer» («he hecho»); «echo», de «echar» («te echo de menos»).")),
        edit(kText, QStringLiteral("despues"), QStringLiteral("después"), catalog::Type::TildeFalta,
             QStringLiteral("«después» lleva tilde: es aguda y termina en «-s» no precedida de otra consonante.")),
    };
}

// Fixes the first edit: only "e" -> "he" (the sample text has "ya e echo").
std::vector<check::Edit> realisticEdits() {
    std::vector<check::Edit> edits = sampleEdits();
    const qsizetype at = kText.indexOf(QStringLiteral(" e echo")) + 1;
    edits[0].start = static_cast<std::size_t>(at);
    edits[0].end = static_cast<std::size_t>(at + 1);
    edits[0].byte_start = static_cast<std::size_t>(kText.left(at).toUtf8().size());
    edits[0].byte_end = edits[0].byte_start + 1;
    edits[0].from = "e";
    edits[0].to = "he";
    return edits;
}

QString renderDir() { return qEnvironmentVariable("REVISA_RENDER_DIR"); }

QString suffix(const QWidget* w) {
    const qreal dpr = w->devicePixelRatioF();
    return dpr > 1.01 ? QStringLiteral("@%1x").arg(dpr) : QString();
}

// Saves the grab of `w` as <dir>/<name>[@2x].png; a translucent window is first composited on a
// neutral background, like a desktop, so the rounded corners and the shadow can be judged.
void save(QWidget* w, const QString& name, bool onBackdrop) {
    const QString dir = renderDir();
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    const QPixmap shot = w->grab();
    QImage out = shot.toImage();
    if (onBackdrop) {
        QImage bg(shot.size(), QImage::Format_ARGB32_Premultiplied);
        bg.setDevicePixelRatio(shot.devicePixelRatio());
        bg.fill(theme::scheme() == theme::Scheme::Dark ? QColor(0x0d, 0x11, 0x17) : QColor(0xe9, 0xec, 0xef));
        QPainter p(&bg);
        p.drawPixmap(0, 0, shot);
        p.end();
        out = bg;
    }
    const QString path = QStringLiteral("%1/%2%3.png").arg(dir, name, suffix(w));
    CHECK(out.save(path));
}

const char* schemeName(theme::Scheme s) { return s == theme::Scheme::Dark ? "dark" : "light"; }

}  // namespace

TEST_CASE("look: colors and contrast per scheme") {
    for (const theme::Scheme s : {theme::Scheme::Light, theme::Scheme::Dark}) {
        const theme::Colors& c = theme::colors(s);
        INFO(schemeName(s));
        // Text >= 4.5:1 on the surfaces it sits on.
        CHECK(theme::contrast(c.text, c.card) >= 4.5);
        CHECK(theme::contrast(c.text, c.window) >= 4.5);
        CHECK(theme::contrast(c.textSecondary, c.card) >= 4.5);
        CHECK(theme::contrast(c.textSecondary, c.panel) >= 4.5);
        CHECK(theme::contrast(c.textSecondary, c.window) >= 4.5);
        CHECK(theme::contrast(c.onAccent, c.buttonFill) >= 4.5);
        CHECK(theme::contrast(c.delFg, c.delBg) >= 4.5);
        CHECK(theme::contrast(c.insFg, c.insBg) >= 4.5);
        CHECK(theme::contrast(c.chipFg, c.chipBg) >= 4.5);
        CHECK(theme::contrast(c.textSecondary, c.keycapBg) >= 4.5);
        // Controls and icons >= 3:1.
        CHECK(theme::contrast(c.accent, c.card) >= 3.0);
        CHECK(theme::contrast(c.textTertiary, c.card) >= 3.0);
        CHECK(theme::contrast(c.success, c.card) >= 3.0);
        CHECK(theme::contrast(c.danger, c.card) >= 3.0);
        CHECK(theme::contrast(c.warning, c.card) >= 3.0);
        // The palette and the stylesheet carry them.
        const QPalette p = theme::palette(c);
        CHECK(p.color(QPalette::Window) == c.window);
        CHECK(p.color(QPalette::Base) == c.card);
        CHECK(p.color(QPalette::Highlight) == c.accent);
        const QString css = theme::styleSheet(c);
        CHECK(css.contains(c.card.name()));
        CHECK(css.contains(c.accent.name()));
        CHECK_FALSE(css.contains(QLatin1Char('@')));  // every placeholder replaced
    }
    CHECK(theme::colors(theme::Scheme::Light).accent == QColor(QStringLiteral("#1f6feb")));
    CHECK(theme::colors(theme::Scheme::Dark).accent == QColor(QStringLiteral("#4493f8")));
    CHECK(theme::colors(theme::Scheme::Light).card == QColor(QStringLiteral("#ffffff")));
    CHECK(theme::colors(theme::Scheme::Dark).card == QColor(QStringLiteral("#1f2329")));
}

TEST_CASE("look: the scheme follows REVISA_TRAY_THEME, then the platform") {
    ensureApp();
    const QByteArray old = qgetenv("REVISA_TRAY_THEME");
    qputenv("REVISA_TRAY_THEME", "dark");
    CHECK(theme::detectScheme(QColor(Qt::white)) == theme::Scheme::Dark);
    qputenv("REVISA_TRAY_THEME", "light");
    CHECK(theme::detectScheme(QColor(Qt::black)) == theme::Scheme::Light);
    qunsetenv("REVISA_TRAY_THEME");
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Unknown) {
        CHECK(theme::detectScheme(QColor(0x20, 0x20, 0x20)) == theme::Scheme::Dark);
        CHECK(theme::detectScheme(QColor(0xf0, 0xf0, 0xf0)) == theme::Scheme::Light);
    }
    if (!old.isNull()) qputenv("REVISA_TRAY_THEME", old);
}

TEST_CASE("look: painted icons are not empty at DPR 1 and 2") {
    ensureApp();
    using icons::Glyph;
    for (const Glyph g : {Glyph::Check, Glyph::Close, Glyph::Copy, Glyph::Settings, Glyph::Pause, Glyph::Play,
                          Glyph::Info, Glyph::Warning, Glyph::Keyboard, Glyph::Minus, Glyph::Plus, Glyph::Download,
                          Glyph::Power}) {
        for (const qreal dpr : {1.0, 2.0}) {
            const QPixmap pm = icons::pixmap(g, Qt::black, 16, dpr);
            INFO(static_cast<int>(g) << " @" << dpr);
            CHECK(pm.width() == static_cast<int>(16 * dpr));
            CHECK(pm.devicePixelRatio() == doctest::Approx(dpr));
            const int ink = inked(pm.toImage());
            CHECK(ink > 0);
            CHECK(ink < pm.width() * pm.height());  // a line icon, not a filled square
        }
        CHECK_FALSE(icons::icon(g, Qt::black).isNull());
    }
    CHECK(inked(icons::dot(Qt::red).pixmap(16).toImage()) > 0);
    const QPixmap app = icons::appIcon(20, 2.0);
    CHECK(app.width() == 40);
    CHECK(inked(app.toImage()) > 0);
}

TEST_CASE("look: popup, toast and settings render offscreen in light and dark without warnings") {
    ensureTheme();
    for (const theme::Scheme s : {theme::Scheme::Light, theme::Scheme::Dark}) {
        INFO(schemeName(s));
        theme::apply(s);
        WarningTrap warnings;
        for (const bool compositor : {true, false}) {
            Popup popup;
            popup.setCompositorProbe([compositor] { return compositor; });
            popup.showResult(kText, realisticEdits());
            settle();
            CHECK(popup.floating() == compositor);
            CHECK(popup.testAttribute(Qt::WA_TranslucentBackground) == compositor);
            REQUIRE(popup.checkBoxes().size() == 3);
            popup.checkBoxes()[1]->setChecked(false);
            CHECK(popup.selectedText() ==
                  QStringLiteral("Hola Ana, ya he echo la compra y después paso por tu casa. ¿Quedamos el lunes?"));
            const QImage shot = popup.grab().toImage();
            CHECK(shot.width() >= 560);
            CHECK(inked(shot) > 0);
            // A click anywhere on a row toggles it, not only on the box.
            QAbstractButton* row = popup.checkBoxes()[1];
            row->click();
            CHECK(row->isChecked());
            popup.showMessage(QStringLiteral("Corrigiendo…"));
            settle();
            CHECK(popup.grab().width() > 0);
            popup.hide();

            Toast toast;
            toast.setCompositorProbe([compositor] { return compositor; });
            toast.showText(QStringLiteral("Sin errores"), Toast::Kind::Success);
            settle();
            CHECK(toast.text() == QStringLiteral("Sin errores"));
            CHECK(inked(toast.grab().toImage()) > 0);
            toast.hide();
        }
        Settings current;
        current.config.hotkey = "Ctrl+Alt+Space";
        SettingsDialog dialog(current, nullptr);
        dialog.show();
        settle();
        CHECK(inked(dialog.grab().toImage()) > 0);
        dialog.hide();
        INFO(warnings.list().join(QLatin1Char('\n')).toStdString());
        CHECK(warnings.list().isEmpty());
    }
    theme::apply(theme::Scheme::Light);
}

// The review renders (only with REVISA_RENDER_DIR).
TEST_CASE("look: render the popup, toast and settings to PNG") {
    if (renderDir().isEmpty()) return;
    ensureTheme();
    for (const theme::Scheme s : {theme::Scheme::Light, theme::Scheme::Dark}) {
        theme::apply(s);
        const QString name = QString::fromLatin1(schemeName(s));
        {
            Popup popup;
            popup.setCompositorProbe([] { return true; });
            popup.showResult(kText, realisticEdits());
            settle();
            save(&popup, QStringLiteral("popup-") + name, true);
            popup.checkBoxes()[1]->setChecked(false);
            popup.checkBoxes()[1]->setFocus(Qt::TabFocusReason);
            settle();
            save(&popup, QStringLiteral("popup-unchecked-") + name, true);
            popup.showMessage(QStringLiteral("Corrigiendo…"));
            settle();
            save(&popup, QStringLiteral("popup-busy-") + name, true);
            popup.hide();
        }
        {
            Popup popup;  // no compositor: opaque, square
            popup.showResult(kText, realisticEdits());
            settle();
            save(&popup, QStringLiteral("popup-opaque-") + name, false);
            popup.hide();
        }
        {
            Toast toast;
            toast.setCompositorProbe([] { return true; });
            const std::pair<const char*, Toast::Kind> kinds[] = {
                {"Sin errores", Toast::Kind::Success},
                {"Copiado: pega con Ctrl+V", Toast::Kind::Copied},
                {"Error: no se pudo cargar el modelo", Toast::Kind::Error},
            };
            int i = 0;
            for (const auto& [text, kind] : kinds) {
                toast.showText(QString::fromUtf8(text), kind);
                settle();
                save(&toast, QStringLiteral("toast-%1-").arg(++i) + name, true);
            }
            toast.hide();
        }
        {
            Settings current;
            current.config.hotkey = "Ctrl+Alt+Space";
            current.config.disabled_types = {catalog::Type::Mayuscula, catalog::Type::ComaSobra};
            SettingsDialog dialog(current, nullptr);
            dialog.show();
            settle();
            save(&dialog, QStringLiteral("settings-") + name, false);
            dialog.hide();
        }
    }
    theme::apply(theme::Scheme::Light);
}
