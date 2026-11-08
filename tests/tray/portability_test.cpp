// Everything OS-specific of revisa-tray lives in src/tray/platform/: the top-level files of
// src/tray/ only use Qt, the standard library and revisa_core.

#include "doctest.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>

TEST_CASE("portability: no POSIX, X11 or Win32 headers outside src/tray/platform/") {
    const QDir dir(QStringLiteral(REVISA_SOURCE_DIR "/src/tray"));
    const QStringList files = dir.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.hpp")}, QDir::Files);
    REQUIRE(files.contains(QStringLiteral("main.cpp")));
    REQUIRE(files.contains(QStringLiteral("app.cpp")));
    const QRegularExpression forbidden(QStringLiteral("#\\s*include\\s*[<\"][^>\"]*(unistd|sys/|X11|xcb|windows\\.h)"),
                                       QRegularExpression::CaseInsensitiveOption);
    for (const QString& name : files) {
        QFile f(dir.filePath(name));
        REQUIRE(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        const QRegularExpressionMatch m = forbidden.match(text);
        INFO(name.toStdString() << ": " << m.captured(0).toStdString());
        CHECK_FALSE(m.hasMatch());
    }
}
