// Field app of the X11 e2e (tests/tray/e2e/run.sh): one QPlainTextEdit standing for "any app"
// where the user selects text and presses the revisa-tray hotkey. No revisa code.
//
//   revisa-field-app --text TEXT --out FILE [--title TITLE] [--mode normal|noprimary|terminal]
//
// The whole text starts selected. Every change of the text is written to FILE (atomically), and
// FILE is also written once at start, so the test knows the app is up.
//   normal     the selection is published as PRIMARY explicitly, as most X11 apps do.
//   noprimary  PRIMARY is left empty: the tray has to fall back to a simulated Ctrl+C.
//   terminal   like normal, but keys behave as in a terminal: Ctrl+V does nothing,
//              Ctrl+Shift+V pastes and Ctrl+Shift+C copies. Launch it with RESOURCE_NAME=xterm so
//              its WM_CLASS is on the tray's terminal list.

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTimer>

#include <cstdio>

namespace {

class Field final : public QPlainTextEdit {
public:
    explicit Field(bool terminal) : terminal_(terminal) {}

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (terminal_ && e->key() == Qt::Key_V && (e->modifiers() & Qt::ControlModifier)) {
            if (e->modifiers() & Qt::ShiftModifier) paste();
            std::fprintf(stderr, "field-app: Ctrl+%sV\n", (e->modifiers() & Qt::ShiftModifier) ? "Shift+" : "");
            return;  // plain Ctrl+V: a terminal would send ^V to the shell, never paste
        }
        if (terminal_ && e->key() == Qt::Key_C && (e->modifiers() & Qt::ControlModifier)) {
            if (e->modifiers() & Qt::ShiftModifier) copy();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }

    // Terminals ignore the standard shortcuts: keep Qt from treating Ctrl+V as one.
    bool event(QEvent* e) override {
        if (terminal_ && e->type() == QEvent::ShortcutOverride) {
            e->accept();
            return true;
        }
        return QPlainTextEdit::event(e);
    }

private:
    bool terminal_;
};

void writeOut(const QString& path, const QString& text) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(text.toUtf8());
    if (!f.commit()) std::fprintf(stderr, "field-app: cannot write %s\n", qPrintable(path));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOptions({{QStringLiteral("text"), QStringLiteral("Initial text."), QStringLiteral("text")},
                       {QStringLiteral("out"), QStringLiteral("File that mirrors the text."), QStringLiteral("file")},
                       {QStringLiteral("title"), QStringLiteral("Window title."), QStringLiteral("title")},
                       {QStringLiteral("mode"), QStringLiteral("normal, noprimary or terminal."), QStringLiteral("mode")}});
    parser.process(app);
    const QString out = parser.value(QStringLiteral("out"));
    const QString mode = parser.value(QStringLiteral("mode")).isEmpty() ? QStringLiteral("normal")
                                                                        : parser.value(QStringLiteral("mode"));
    if (out.isEmpty() || (mode != QLatin1String("normal") && mode != QLatin1String("noprimary") &&
                          mode != QLatin1String("terminal"))) {
        std::fprintf(stderr, "usage: revisa-field-app --text TEXT --out FILE [--title T] [--mode normal|noprimary|terminal]\n");
        return 2;
    }

    Field field(mode == QLatin1String("terminal"));
    field.setWindowTitle(parser.value(QStringLiteral("title")).isEmpty() ? QStringLiteral("revisa-field")
                                                                         : parser.value(QStringLiteral("title")));
    field.setPlainText(parser.value(QStringLiteral("text")));
    field.resize(640, 240);
    writeOut(out, field.toPlainText());
    QObject::connect(&field, &QPlainTextEdit::textChanged, &field, [&field, out] {
        writeOut(out, field.toPlainText());
        std::fprintf(stderr, "field-app: text now: %s\n", qPrintable(field.toPlainText()));
    });
    field.show();

    // Once the window is mapped: select everything and publish (or not) the PRIMARY selection.
    QTimer::singleShot(0, &field, [&field, mode] {
        field.selectAll();
        QClipboard* cb = QGuiApplication::clipboard();
        if (mode == QLatin1String("noprimary")) cb->clear(QClipboard::Selection);
        else cb->setText(field.textCursor().selectedText(), QClipboard::Selection);
        std::fprintf(stderr, "field-app: ready (%s)\n", qPrintable(mode));
    });
    return app.exec();
}
