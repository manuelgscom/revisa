// Start with the session on Linux: $XDG_CONFIG_HOME/autostart/revisa.desktop (XDG Autostart),
// which works the same under X11 and Wayland.

#include "tray/platform/platform.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace revisa::tray {

namespace {

QString autostartFile() {
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/autostart/revisa.desktop");
}

// Exec value per the desktop entry spec: a quoted argument where " ` $ \ are escaped with a
// backslash, then the string-level escape doubles every backslash; % becomes %%.
QString quoteExec(const QString& path) {
    QString out = QStringLiteral("\"");
    for (const QChar c : path) {
        if (c == u'"' || c == u'`' || c == u'$') {
            out += QStringLiteral("\\\\");
            out += c;
        } else if (c == u'\\') {
            out += QStringLiteral("\\\\\\\\");
        } else if (c == u'%') {
            out += QStringLiteral("%%");
        } else {
            out += c;
        }
    }
    return out + u'"';
}

}  // namespace

bool autostartEnabled() {
    QFile file(autostartFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line == "Hidden=true" || line == "X-GNOME-Autostart-enabled=false") return false;
    }
    return true;
}

bool setAutostart(bool enabled, QString* error) {
    const QString path = autostartFile();
    if (!enabled) {
        if (!QFileInfo::exists(path) || QFile::remove(path)) return true;
        if (error) *error = QStringLiteral("No se pudo borrar %1.").arg(path);
        return false;
    }

    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (error) *error = QStringLiteral("No se pudo crear la carpeta %1.").arg(dir);
        return false;
    }
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty()) exe = QCoreApplication::applicationFilePath();

    const QString content = QStringLiteral(
                                "[Desktop Entry]\n"
                                "Type=Application\n"
                                "Name=revisa\n"
                                "Comment=Corrector de ortografía y gramática\n"
                                "Exec=%1\n"
                                "Icon=revisa\n"
                                "Terminal=false\n"
                                "X-GNOME-Autostart-enabled=true\n")
                                .arg(quoteExec(exe));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(content.toUtf8()) < 0 ||
        !file.commit()) {
        if (error) *error = QStringLiteral("No se pudo escribir %1.").arg(path);
        return false;
    }
    return true;
}

}  // namespace revisa::tray
