// The parts of revisa-tray that run without a real display (offscreen): command line, single
// instance, the engine worker with the fake engine, the popup and the settings dialog.

#include "doctest.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QListWidget>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

#include <optional>
#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/check/check.hpp"
#include "tray/diff_html.hpp"
#include "tray/engine_worker.hpp"
#include "tray/instance.hpp"
#include "tray/popup.hpp"
#include "tray/settings_dialog.hpp"

using namespace revisa;
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

// Sets (or unsets, with nullopt) an environment variable for the scope.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::optional<QByteArray> value) : name_(name) {
        if (qEnvironmentVariableIsSet(name)) old_ = qgetenv(name);
        if (value) qputenv(name, *value);
        else qunsetenv(name);
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

// Processes events until pred() or the timeout.
template <class Pred>
bool waitFor(Pred pred, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

check::Edit edit(const QString& text, qsizetype start, qsizetype end, const QString& to, catalog::Type type,
                 const char* why = nullptr) {
    check::Edit e;
    e.start = static_cast<std::size_t>(start);
    e.end = static_cast<std::size_t>(end);
    e.byte_start = static_cast<std::size_t>(text.left(start).toUtf8().size());
    e.byte_end = static_cast<std::size_t>(text.left(end).toUtf8().size());
    e.from = text.mid(start, end - start).toStdString();
    e.to = to.toStdString();
    e.type = type;
    if (why) e.explanation = why;
    return e;
}

void press(QWidget* w, int key) {
    QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

}  // namespace

// --- Command line and single instance -----------------------------------------------------------

TEST_CASE("parseArgs") {
    CHECK(parseArgs({}).command == Command::Activate);
    CHECK(parseArgs({QStringLiteral("--check-clipboard")}).command == Command::CheckClipboard);
    CHECK(parseArgs({QStringLiteral("--quit")}).command == Command::Quit);
    CHECK(parseArgs({QStringLiteral("--version")}).version);
    // Qt's own options are left alone.
    const Invocation qt = parseArgs({QStringLiteral("-platform"), QStringLiteral("offscreen")});
    CHECK(qt.error.isEmpty());
    CHECK(qt.command == Command::Activate);
    CHECK_FALSE(parseArgs({QStringLiteral("--nope")}).error.isEmpty());
    CHECK_FALSE(parseArgs({QStringLiteral("--quit"), QStringLiteral("--check-clipboard")}).error.isEmpty());
}

TEST_CASE("command names round-trip") {
    for (const Command c : {Command::Activate, Command::CheckClipboard, Command::Quit})
        CHECK(commandFromName(commandName(c) + QStringLiteral("\n")) == std::optional<Command>(c));
    CHECK_FALSE(commandFromName(QStringLiteral("rm -rf")).has_value());
}

TEST_CASE("single instance: the second one hands its command to the first") {
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/inst");

    SingleInstance first(dir);
    REQUIRE(first.tryPrimary());
    SingleInstance second(dir);
    CHECK_FALSE(second.tryPrimary());

    // Written before listen(): a second instance that raced with the start.
    REQUIRE(second.send(Command::CheckClipboard));
    std::vector<Command> got;
    QObject::connect(&first, &SingleInstance::received, [&](Command c) { got.push_back(c); });
    first.listen();
    REQUIRE(waitFor([&] { return got.size() == 1; }));
    CHECK(got[0] == Command::CheckClipboard);

    REQUIRE(second.send(Command::Quit));
    REQUIRE(waitFor([&] { return got.size() == 2; }));
    CHECK(got[1] == Command::Quit);
    CHECK(QDir(dir).entryList({QStringLiteral("*.cmd")}, QDir::Files).isEmpty());

    // Once the first one is gone, the next one becomes the primary.
    first.release();
    SingleInstance third(dir);
    CHECK(third.tryPrimary());
}

// --- Engine worker (fake engine) ----------------------------------------------------------------

#if REVISA_TEST_HOOKS
TEST_CASE("engine worker: warm-up, check and errors with the fake engine") {
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
    ScopedEnv noError("REVISA_FAKE_ERROR", std::nullopt);
    ScopedEnv noOutput("REVISA_FAKE_OUTPUT", std::nullopt);
    ScopedEnv fakeMap("REVISA_FAKE_MAP", map.toUtf8());
    CHECK(fakeEngineRequested());

    EngineWorker worker;  // driven directly on this thread: the app moves it to a QThread
    int ready = 0;
    QString failure;
    std::vector<std::pair<quint64, check::Result>> results;
    QObject::connect(&worker, &EngineWorker::ready, [&](int g) { ready = g; });
    QObject::connect(&worker, &EngineWorker::failed, [&](int, const QString& m) { failure = m; });
    QObject::connect(&worker, &EngineWorker::finished,
                     [&](quint64 id, const check::Result& r) { results.emplace_back(id, r); });

    worker.start(7, QString(), 0);
    CHECK(ready == 7);
    CHECK(failure.isEmpty());

    const QString text = QStringLiteral("Hola Ana, haber si mañana podemos vernos un rato.");
    worker.check(1, text, check::Options{});
    REQUIRE(results.size() == 1);
    CHECK(results[0].first == 1);
    CHECK(results[0].second.error.empty());
    REQUIRE(results[0].second.edits.size() == 1);
    CHECK(results[0].second.edits[0].type == catalog::Type::HaberAVer);
    CHECK(results[0].second.edits[0].start == 10);

    // Disabled types come from the options.
    check::Options off;
    off.disabled = {catalog::Type::HaberAVer};
    worker.check(2, text, off);
    REQUIRE(results.size() == 2);
    CHECK(results[1].second.edits.empty());

    // Engine errors fail the warm-up with a Spanish message.
    ScopedEnv error("REVISA_FAKE_ERROR", QByteArray("load_failed"));
    worker.start(8, QString(), 0);
    CHECK(ready == 7);
    CHECK(failure.startsWith(QStringLiteral("no se pudo cargar el modelo")));
}
#endif

TEST_CASE("engine worker: without an engine every check fails") {
    ensureApp();
    EngineWorker worker;
    std::optional<check::Result> got;
    QObject::connect(&worker, &EngineWorker::finished, [&](quint64, const check::Result& r) { got = r; });
    worker.check(3, QStringLiteral("Hola."), check::Options{});
    REQUIRE(got);
    CHECK(got->error == "load_failed");
    CHECK(engineErrorText(*got).startsWith(QStringLiteral("no se pudo cargar el modelo")));
}

// --- Popup ---------------------------------------------------------------------------------------

TEST_CASE("popup: checkboxes drive the diff and the text; Enter applies, Esc cancels") {
    ensureApp();
    Popup popup;
    CHECK(popup.objectName() == QStringLiteral("revisa-popup"));
    CHECK(popup.windowTitle() == QStringLiteral("revisa"));

    const QString t = QStringLiteral("Hola Ana, haber si mañana e echo la tarea.");
    const qsizetype h = t.indexOf(QStringLiteral("haber"));
    const qsizetype e = t.indexOf(QStringLiteral("e echo"));
    popup.showResult(t, {edit(t, h, h + 5, QStringLiteral("a ver"), catalog::Type::HaberAVer, "«a ver» es el verbo «ver»."),
                         edit(t, e, e + 6, QStringLiteral("he hecho"), catalog::Type::EchoHecho)});
    CHECK(popup.isVisible());
    CHECK(popup.hasResult());
    REQUIRE(popup.checkBoxes().size() == 2);
    CHECK(popup.checkBoxes()[0]->text() == QStringLiteral("«haber» → «a ver»"));
    CHECK(popup.checkBoxes()[0]->isChecked());
    CHECK(popup.selectedText() == QStringLiteral("Hola Ana, a ver si mañana he hecho la tarea."));
    CHECK(popup.diffText().contains(QStringLiteral(">a ver<")));

    popup.checkBoxes()[0]->setChecked(false);
    CHECK(popup.selectedText() == QStringLiteral("Hola Ana, haber si mañana he hecho la tarea."));
    CHECK_FALSE(popup.diffText().contains(QStringLiteral(">a ver<")));
    CHECK(popup.diffText().contains(QStringLiteral(">he hecho<")));

    QString accepted;
    int cancelled = 0;
    QObject::connect(&popup, &Popup::accepted, [&](const QString& s) { accepted = s; });
    QObject::connect(&popup, &Popup::cancelled, [&] { ++cancelled; });
    press(&popup, Qt::Key_Return);
    CHECK(accepted == QStringLiteral("Hola Ana, haber si mañana he hecho la tarea."));
    // Return on a focused checkbox reaches the popup too.
    accepted.clear();
    press(popup.checkBoxes()[1], Qt::Key_Enter);
    CHECK(accepted == QStringLiteral("Hola Ana, haber si mañana he hecho la tarea."));
    press(&popup, Qt::Key_Escape);
    CHECK(cancelled == 1);
    // A close from the window manager (Alt+F4) cancels too, and leaves the hiding to the app.
    CHECK_FALSE(popup.close());
    CHECK(cancelled == 2);
    CHECK(popup.isVisible());

    // A new result replaces the old edits.
    popup.showResult(QStringLiteral("Que tal?"), {edit(QStringLiteral("Que tal?"), 0, 0, QStringLiteral("¿"),
                                                        catalog::Type::SignoApertura)});
    CHECK(popup.checkBoxes().size() == 1);
    CHECK(popup.selectedText() == QStringLiteral("¿Que tal?"));

    // While it only shows a message, Enter does nothing and Esc still cancels.
    accepted.clear();
    popup.showMessage(QStringLiteral("Leyendo el portapapeles…"));
    CHECK_FALSE(popup.hasResult());
    CHECK(popup.checkBoxes().empty());
    press(&popup, Qt::Key_Return);
    CHECK(accepted.isEmpty());
    press(&popup, Qt::Key_Escape);
    CHECK(cancelled == 3);
    popup.hide();
}

// --- Settings ------------------------------------------------------------------------------------

TEST_CASE("settings dialog: values and accept") {
    ensureApp();
    Settings current;
    current.config.hotkey = "Ctrl+Alt+Space";
    current.config.threads = 1;
    current.config.disabled_types = {catalog::Type::Mayuscula};
    current.autostart = false;

    std::optional<Settings> applied;
    SettingsDialog dialog(current, [&](const Settings& s, QString*) {
        applied = s;
        return true;
    });
    const Settings same = dialog.values();
    CHECK(same.config == current.config);
    CHECK(same.autostart == current.autostart);

    auto* types = dialog.findChild<QListWidget*>();
    REQUIRE(types);
    CHECK(types->count() == static_cast<int>(catalog::kTypeCount) - 1);  // all but OTRO
    for (int row = 0; row < types->count(); ++row) {
        QListWidgetItem* item = types->item(row);
        const auto type = static_cast<catalog::Type>(item->data(Qt::UserRole).toInt());
        CHECK(item->text() == typeLabel(type));  // the Spanish name; the id is the tooltip
        CHECK(item->toolTip() == QString::fromLatin1(catalog::type_id(type)));
        if (type == catalog::Type::Mayuscula) {
            CHECK(item->checkState() == Qt::Unchecked);
            item->setCheckState(Qt::Checked);
        }
        if (type == catalog::Type::Espaciado) item->setCheckState(Qt::Unchecked);
    }
    auto* threads = dialog.findChild<QSpinBox*>();
    REQUIRE(threads);
    threads->setValue(0);

    dialog.accept();
    CHECK(dialog.result() == QDialog::Accepted);
    REQUIRE(applied);
    CHECK(applied->config.threads == 0);
    CHECK(applied->config.hotkey == "Ctrl+Alt+Space");
    CHECK(applied->config.disabled_types == std::set<catalog::Type>{catalog::Type::Espaciado});
}

TEST_CASE("settings dialog: more threads than cores are kept, not clamped") {
    ensureApp();
    Settings current;
    current.config.threads = 200;  // more than any test machine has
    SettingsDialog dialog(current, nullptr);
    auto* threads = dialog.findChild<QSpinBox*>();
    REQUIRE(threads);
    CHECK(threads->value() == 200);
    CHECK(dialog.values().config.threads == 200);
}
