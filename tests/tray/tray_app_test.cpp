// TrayApp end to end without a display: clipboard-only platform (offscreen), fake engine,
// «Corregir el portapapeles» → popup → Enter → corrected text in the clipboard. The X11 hotkey
// flow is the e2e's job (tests/tray/e2e, Xvfb).

#include "doctest.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QMenu>
#include <QSpinBox>
#include <QTemporaryDir>

#include <optional>

#include "tray/app.hpp"
#include "tray/popup.hpp"
#include "tray/settings_dialog.hpp"
#include "tray/toast.hpp"

using namespace revisa::tray;

namespace {

void ensureApp() {
    if (QCoreApplication::instance()) return;
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static int argc = 1;
    static char arg0[] = "revisa_tray_tests";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

class Env {
public:
    Env(const char* name, std::optional<QByteArray> value) : name_(name) {
        if (qEnvironmentVariableIsSet(name)) old_ = qgetenv(name);
        if (value) qputenv(name, *value);
        else qunsetenv(name);
    }
    ~Env() {
        if (old_) qputenv(name_, *old_);
        else qunsetenv(name_);
    }
    Env(const Env&) = delete;
    Env& operator=(const Env&) = delete;

private:
    const char* name_;
    std::optional<QByteArray> old_;
};

template <class Pred>
bool waitFor(Pred pred, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

template <class W>
W* topLevel(const char* objectName) {
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->objectName() == QLatin1String(objectName)) return qobject_cast<W*>(w);
    return nullptr;
}

}  // namespace

TEST_CASE("resources: the tray icons are embedded") {
    Q_INIT_RESOURCE(tray);
    for (const int size : {16, 22, 24, 32, 48, 64, 128, 256})
        CHECK(QFile::exists(QStringLiteral(":/revisa/revisa-%1.png").arg(size)));
}

#if REVISA_TEST_HOOKS
TEST_CASE("tray app: clipboard check with the fake engine") {
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString map = tmp.filePath(QStringLiteral("map.json"));
    {
        QFile f(map);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("{\"Hola Ana, haber si mañana podemos vernos un rato.\": "
                               "\"Hola Ana, a ver si mañana podemos vernos un rato.\"}")
                    .toUtf8());
    }
    Env config("XDG_CONFIG_HOME", (tmp.path() + QStringLiteral("/config")).toUtf8());
    Env data("XDG_DATA_HOME", (tmp.path() + QStringLiteral("/data")).toUtf8());
#ifdef Q_OS_WIN
    // Windows reads %APPDATA%\revisa\config and %LOCALAPPDATA%\revisa\models: same folders.
    Env appdata("APPDATA", (tmp.path() + QStringLiteral("/config")).toUtf8());
    Env localappdata("LOCALAPPDATA", (tmp.path() + QStringLiteral("/data")).toUtf8());
#endif
    Env platform("REVISA_TRAY_PLATFORM", QByteArray("clipboard"));
    Env fakeError("REVISA_FAKE_ERROR", std::nullopt);
    Env fakeOutput("REVISA_FAKE_OUTPUT", std::nullopt);
    Env fakeMap("REVISA_FAKE_MAP", map.toUtf8());

    QClipboard* cb = QGuiApplication::clipboard();
    cb->setText(QStringLiteral("Hola Ana, haber si mañana podemos vernos un rato."));

    TrayApp app;
    app.start();
    app.run(Command::CheckClipboard);  // waits for the engine if it is still loading

    Popup* popup = topLevel<Popup>("revisa-popup");
    REQUIRE(popup);
    REQUIRE(waitFor([&] { return popup->isVisible() && popup->hasResult(); }));
    CHECK(popup->checkBoxes().size() == 1);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(popup, &enter);
    CHECK_FALSE(popup->isVisible());
    CHECK(cb->text() == QStringLiteral("Hola Ana, a ver si mañana podemos vernos un rato."));
    Toast* toast = topLevel<Toast>("revisa-toast");
    REQUIRE(toast);
    CHECK(toast->text() == QStringLiteral("Copiado: pega con Ctrl+V"));

    // A correct text: no popup, «Sin errores».
    app.run(Command::CheckClipboard);
    REQUIRE(waitFor([&] { return toast->text() == QStringLiteral("Sin errores"); }));
    CHECK_FALSE(popup->isVisible());

    // Over the server's limit.
    cb->setText(QString(20001, QLatin1Char('a')));
    app.run(Command::CheckClipboard);
    REQUIRE(waitFor([&] { return toast->text().startsWith(QStringLiteral("Texto demasiado largo")); }));
    CHECK_FALSE(popup->isVisible());

    // Esc while the popup is up leaves the clipboard alone.
    cb->setText(QStringLiteral("Hola Ana, haber si mañana podemos vernos un rato."));
    app.run(Command::CheckClipboard);
    REQUIRE(waitFor([&] { return popup->isVisible() && popup->hasResult(); }));
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(popup, &esc);
    CHECK_FALSE(popup->isVisible());
    CHECK(cb->text() == QStringLiteral("Hola Ana, haber si mañana podemos vernos un rato."));
}

TEST_CASE("tray app: settings keep the keys edited by hand since startup") {
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    Env config("XDG_CONFIG_HOME", (tmp.path() + QStringLiteral("/config")).toUtf8());
    Env data("XDG_DATA_HOME", (tmp.path() + QStringLiteral("/data")).toUtf8());
#ifdef Q_OS_WIN
    // Windows reads %APPDATA%\revisa\config and %LOCALAPPDATA%\revisa\models: same folders.
    Env appdata("APPDATA", (tmp.path() + QStringLiteral("/config")).toUtf8());
    Env localappdata("LOCALAPPDATA", (tmp.path() + QStringLiteral("/data")).toUtf8());
#endif
    Env platform("REVISA_TRAY_PLATFORM", QByteArray("clipboard"));
    Env fakeError("REVISA_FAKE_ERROR", std::nullopt);
    Env fakeMap("REVISA_FAKE_MAP", std::nullopt);
    Env fakeOutput("REVISA_FAKE_OUTPUT", QByteArray("x"));
    const QString path = tmp.path() + QStringLiteral("/config/revisa/config");
    auto writeConfig = [&](const QByteArray& text) {
        REQUIRE(QDir().mkpath(tmp.path() + QStringLiteral("/config/revisa")));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    };
    writeConfig("threads = 2\n");

    TrayApp app;
    app.start();
    writeConfig("threads = 2\nport = 9999\n");  // edited by hand while the app runs

    QAction* settingsAction = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* menu = qobject_cast<QMenu*>(w))
            for (QAction* a : menu->actions())
                if (a->text() == QStringLiteral("Ajustes…")) settingsAction = a;
    REQUIRE(settingsAction);
    settingsAction->trigger();
    auto* dialog = topLevel<SettingsDialog>("revisa-settings");
    REQUIRE(dialog);
    auto* threads = dialog->findChild<QSpinBox*>();
    REQUIRE(threads);
    CHECK(threads->value() == 2);
    threads->setValue(1);
    dialog->accept();

    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QString saved = QString::fromUtf8(f.readAll());
    CHECK(saved.contains(QStringLiteral("threads = 1")));
    CHECK(saved.contains(QStringLiteral("port = 9999")));
}
#endif
