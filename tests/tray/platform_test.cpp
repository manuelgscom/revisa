// Headless parts of the OS layer (src/tray/platform/): the clipboard-only platform, autostart
// (.desktop on Linux, the HKCU Run value on Windows) and, on Linux, the pure X11 helpers (the
// Win32 ones are in keys_win32_test.cpp). Nothing here touches a real display.

#include "doctest.h"

#include "tray/platform/platform.hpp"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <optional>

#ifdef Q_OS_LINUX
#include "tray/platform/keys_linux.hpp"
// X headers last: they define macros that clash with Qt names.
#include <X11/X.h>
#include <X11/keysym.h>
#endif

#ifdef Q_OS_WIN
#include <QDir>
#include <QSettings>
#endif

using namespace revisa::tray;

namespace {

// One QApplication for the whole test binary, always offscreen (never the user's display).
void ensureApp() {
    if (QCoreApplication::instance()) return;
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static int argc = 1;
    static char arg0[] = "revisa_tray_tests";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

// Sets an environment variable for the scope and restores the previous value.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const QByteArray& value) : name_(name) {
        if (qEnvironmentVariableIsSet(name)) old_ = qgetenv(name);
        qputenv(name, value);
    }
    ~ScopedEnv() {
        if (old_) qputenv(name_, *old_);
        else qunsetenv(name_);
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    std::optional<QByteArray> old_;
};

[[maybe_unused]] QByteArray readAll(const QString& path) {  // Linux cases only
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST_CASE("platform: offscreen falls back to the clipboard-only platform") {
    ensureApp();
    const auto p = makePlatform();
    REQUIRE(p);
    CHECK(p->name() == "clipboard");
    CHECK_FALSE(p->canSimulateInput());
    CHECK(p->activeWindow() == 0);
    CHECK_FALSE(p->activate(0));

    QString error;
    CHECK_FALSE(p->registerHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+Space")), &error));
    CHECK(error.contains(QStringLiteral("--check-clipboard")));

    // paste leaves the text in the clipboard and reports it was not pasted.
    CHECK_FALSE(p->paste(0, QStringLiteral("Hola, ¿qué tal?")));
    CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("Hola, ¿qué tal?"));
    CHECK(p->readSelection(0) == std::optional<QString>(QStringLiteral("Hola, ¿qué tal?")));

    QGuiApplication::clipboard()->setText(QStringLiteral("  \n"));
    CHECK_FALSE(p->readSelection(0).has_value());
}

TEST_CASE("platform: REVISA_TRAY_PLATFORM=clipboard forces clipboard-only") {
    ensureApp();
    ScopedEnv env("REVISA_TRAY_PLATFORM", "clipboard");
    CHECK(makePlatform()->name() == "clipboard");
}

#ifdef Q_OS_LINUX

TEST_CASE("platform: autostart .desktop round trip") {
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ScopedEnv config("XDG_CONFIG_HOME", dir.path().toUtf8());
    ScopedEnv appimage("APPIMAGE", "/opt/mis apps/revisa$1 \"x\".AppImage");
    const QString file = dir.path() + "/autostart/revisa.desktop";

    CHECK_FALSE(autostartEnabled());
    QString error;
    REQUIRE(setAutostart(true, &error));
    CHECK(error.isEmpty());
    CHECK(autostartEnabled());

    const QByteArray content = readAll(file);
    CHECK(content.startsWith("[Desktop Entry]\n"));
    CHECK(content.contains("\nType=Application\n"));
    CHECK(content.contains("\nIcon=revisa\n"));
    CHECK(content.contains(R"(Exec="/opt/mis apps/revisa\\$1 \\"x\\".AppImage")"));

    // A user (or GNOME's switch) disabling it without deleting the file.
    {
        QFile f(file);
        REQUIRE(f.open(QIODevice::Append));
        f.write("Hidden=true\n");
    }
    CHECK_FALSE(autostartEnabled());

    REQUIRE(setAutostart(false, &error));
    CHECK_FALSE(QFile::exists(file));
    CHECK_FALSE(autostartEnabled());
    CHECK(setAutostart(false, &error));  // already off
}

TEST_CASE("platform: autostart without APPIMAGE uses this binary; errors are reported") {
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    qunsetenv("APPIMAGE");
    {
        ScopedEnv config("XDG_CONFIG_HOME", dir.path().toUtf8());
        REQUIRE(setAutostart(true, nullptr));
        const QByteArray content = readAll(dir.path() + "/autostart/revisa.desktop");
        CHECK(content.contains("Exec=\"" + QCoreApplication::applicationFilePath().toUtf8() + "\"\n"));
    }
    {
        // XDG_CONFIG_HOME is a file: the autostart folder cannot be created.
        const QString blocker = dir.path() + "/not-a-dir";
        QFile f(blocker);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.close();
        ScopedEnv config("XDG_CONFIG_HOME", blocker.toUtf8());
        QString error;
        CHECK_FALSE(setAutostart(true, &error));
        CHECK(error.startsWith(QStringLiteral("No se pudo")));
    }
}

TEST_CASE("platform: Qt key sequence to X keysym and modifiers") {
    using revisa::tray::x11::chordFromKeys;
    auto chord = [](const QKeySequence& k) {
        const auto c = chordFromKeys(k);
        return std::pair<unsigned long, unsigned int>(c.keysym, c.mods);
    };
    using P = std::pair<unsigned long, unsigned int>;
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+Alt+Space"))) == P(XK_space, ControlMask | Mod1Mask));
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+Shift+A"))) == P(XK_a, ControlMask | ShiftMask));
    CHECK(chord(QKeySequence(QStringLiteral("Alt+Z"))) == P(XK_z, Mod1Mask));
    CHECK(chord(QKeySequence(QStringLiteral("Meta+5"))) == P(XK_5, Mod4Mask));
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+0"))) == P(XK_0, ControlMask));
    CHECK(chord(QKeySequence(QStringLiteral("F1"))) == P(XK_F1, 0u));
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+F12"))) == P(XK_F12, ControlMask));
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+Return"))) == P(XK_Return, ControlMask));
    CHECK(chord(QKeySequence(QStringLiteral("Alt+Tab"))) == P(XK_Tab, Mod1Mask));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Comma)) == P(XK_comma, ControlMask));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Period)) == P(XK_period, ControlMask | Mod1Mask));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Minus)) == P(XK_minus, ControlMask));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Slash)) == P(XK_slash, ControlMask));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Ntilde)) == P(XK_ntilde, ControlMask));
    CHECK(chord(QKeySequence(Qt::ALT | Qt::Key_Escape)) == P(XK_Escape, Mod1Mask));
    // Only the first chord counts; no key / unknown key -> keysym 0.
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C"))) == P(XK_k, ControlMask));
    CHECK(chord(QKeySequence()) == P(0ul, 0u));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_VolumeUp)).first == 0ul);
}

TEST_CASE("platform: terminal detection by WM_CLASS") {
    using revisa::tray::x11::isTerminalClass;
    CHECK(isTerminalClass(u"xterm", u"XTerm"));
    CHECK(isTerminalClass(u"gnome-terminal-server", u"Gnome-terminal"));
    CHECK(isTerminalClass(u"konsole", u"konsole"));
    CHECK(isTerminalClass(u"Alacritty", u"Alacritty"));
    CHECK(isTerminalClass(u"urxvt", u"URxvt"));
    CHECK(isTerminalClass(u"whatever", u"kitty"));
    CHECK(isTerminalClass(u"kgx", u"org.gnome.Console"));
    CHECK_FALSE(isTerminalClass(u"Navigator", u"firefox"));
    CHECK_FALSE(isTerminalClass(u"revisa-field-app", u"revisa-field-app"));
    CHECK_FALSE(isTerminalClass(u"xterminal", u"stx"));  // whole names only
    CHECK_FALSE(isTerminalClass(u"", u""));
}

#endif

#ifdef Q_OS_WIN

namespace {

const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// The "revisa" Run value, read with a fresh QSettings each time; nullopt if absent.
std::optional<QString> runValue() {
    const QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (!run.contains(QStringLiteral("revisa"))) return std::nullopt;
    return run.value(QStringLiteral("revisa")).toString();
}

void setRunValue(const std::optional<QString>& value) {
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (value) run.setValue(QStringLiteral("revisa"), *value);
    else run.remove(QStringLiteral("revisa"));
    run.sync();
}

// Keeps the user's real "revisa" Run value (the installer may have written it) across a test.
class ScopedRunValue {
public:
    ScopedRunValue() : old_(runValue()) {}
    ~ScopedRunValue() { setRunValue(old_); }
    ScopedRunValue(const ScopedRunValue&) = delete;
    ScopedRunValue& operator=(const ScopedRunValue&) = delete;

private:
    std::optional<QString> old_;
};

}  // namespace

TEST_CASE("platform: autostart HKCU Run value round trip") {
    ensureApp();
    ScopedRunValue keep;
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    REQUIRE(setAutostart(false, nullptr));
    CHECK_FALSE(runValue().has_value());
    CHECK_FALSE(autostartEnabled());

    QString error;
    REQUIRE(setAutostart(true, &error));
    CHECK(error.isEmpty());
    CHECK(runValue() == std::optional<QString>(QStringLiteral("\"%1\"").arg(exe)));
    CHECK(autostartEnabled());

    REQUIRE(setAutostart(false, &error));
    CHECK_FALSE(runValue().has_value());
    CHECK_FALSE(autostartEnabled());
    CHECK(setAutostart(false, &error));  // already off
}

TEST_CASE("platform: autostart Run value written by hand or stale") {
    ensureApp();
    ScopedRunValue keep;
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    setRunValue(exe);  // unquoted, as some tools write it
    CHECK(autostartEnabled());
    setRunValue(QStringLiteral("\"%1\" --check-clipboard").arg(exe));  // with arguments
    CHECK(autostartEnabled());
    // Points to a binary that is gone: reads as off, so the Settings switch can rewrite it.
    setRunValue(QStringLiteral("\"C:\\no-existe\\revisa-tray.exe\""));
    CHECK_FALSE(autostartEnabled());
    REQUIRE(setAutostart(true, nullptr));
    CHECK(runValue() == std::optional<QString>(QStringLiteral("\"%1\"").arg(exe)));
}

#endif
