// revisa-tray: Spanish grammar checker in the system tray.
//
//   revisa-tray [--check-clipboard | --quit | --version]

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

#include "core/infer/cpu.hpp"
#include "tray/app.hpp"
#include "tray/instance.hpp"
#include "tray/theme.hpp"

using namespace revisa::tray;

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(tray);  // the icons live in revisa_tray_lib, a static library
    // Our own flags first, so --version and a bad command line need no display.
    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    const Invocation inv = parseArgs(args);
    if (!inv.error.isEmpty()) {
        std::fprintf(stderr, "revisa-tray: %s\nUso: revisa-tray [--check-clipboard | --quit | --version]\n",
                     inv.error.toLocal8Bit().constData());
        return 2;
    }
    if (inv.version) {
        std::printf("revisa-tray %s\n", REVISA_VERSION);
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("revisa"));
    QApplication::setApplicationVersion(QStringLiteral(REVISA_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("revisa"));
    QApplication::setQuitOnLastWindowClosed(false);
    QIcon icon;
    for (const int size : {16, 22, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/revisa/revisa-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);
    theme::install(app);  // Fusion + revisa's palette (light or dark, as the desktop), before any window

    // Qt's own strings (context menus, key names) in Spanish when the translation is there.
    QTranslator qtStrings;
    if (qtStrings.load(QLocale(QLocale::Spanish), QStringLiteral("qtbase"), QStringLiteral("_"),
                       QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtStrings);

    // The release forces AVX2 + FMA + F16C: refuse to start instead of dying on the first check.
    if (!revisa::infer::cpu_supports_release()) {
        QMessageBox::critical(nullptr, QStringLiteral("revisa"),
                              QStringLiteral("Este procesador no tiene AVX2 (con FMA y F16C), que revisa necesita."));
        return 2;
    }

    SingleInstance instance(defaultInstanceDir());
    if (!instance.tryPrimary()) {
        const bool sent = instance.send(inv.command);
        qInfo("revisa-tray: %s %s to the running instance", sent ? "sent" : "could not send",
              qPrintable(commandName(inv.command)));
        return sent ? 0 : 1;
    }
    if (inv.command == Command::Quit) {
        std::fprintf(stderr, "revisa-tray: no hay ninguna instancia en marcha\n");
        return 0;
    }

    TrayApp tray;
    QObject::connect(&instance, &SingleInstance::received, &tray, &TrayApp::run);
    instance.listen();
    QTimer::singleShot(0, &tray, [&tray, command = inv.command] {
        tray.start();
        if (command == Command::CheckClipboard) tray.run(command);
    });
    qInfo("revisa-tray: started (version %s)", REVISA_VERSION);
    return app.exec();
}
