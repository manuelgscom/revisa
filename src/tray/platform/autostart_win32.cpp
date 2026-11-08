// Start with the session on Windows: the value "revisa" of HKCU\Software\Microsoft\Windows\
// CurrentVersion\Run. The Inno Setup installer's «Arrancar con Windows» task writes (and its
// uninstaller deletes) the very same value, so the installer and the Settings switch agree.

#include "tray/platform/platform.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace revisa::tray {

namespace {

const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char kValueName[] = "revisa";

// The program of a Run command line: the quoted part, or everything if it is not quoted.
QString programOf(const QString& command) {
    const QString c = command.trimmed();
    if (!c.startsWith(u'"')) return c;
    const qsizetype end = c.indexOf(u'"', 1);
    return end < 0 ? c.mid(1) : c.mid(1, end - 1);
}

}  // namespace

// Enabled = the value exists and points to an existing file. A stale value (the app was moved or
// uninstalled by hand) reads as off, so turning the switch on rewrites it with this binary.
bool autostartEnabled() {
    const QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    const QString program = programOf(run.value(QString::fromLatin1(kValueName)).toString());
    return !program.isEmpty() && QFileInfo(program).isFile();
}

bool setAutostart(bool enabled, QString* error) {
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    const QString name = QString::fromLatin1(kValueName);
    if (enabled) {
        run.setValue(name, QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    } else {
        run.remove(name);
    }
    run.sync();
    if (run.status() == QSettings::NoError) return true;
    if (error) {
        *error = enabled ? QStringLiteral("No se pudo escribir el arranque con Windows en el registro (clave Run).")
                         : QStringLiteral("No se pudo quitar el arranque con Windows del registro (clave Run).");
    }
    return false;
}

}  // namespace revisa::tray
