#include "tray/instance.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <vector>

namespace revisa::tray {

QString commandName(Command c) {
    switch (c) {
        case Command::Activate: return QStringLiteral("activate");
        case Command::CheckClipboard: return QStringLiteral("check-clipboard");
        case Command::Quit: return QStringLiteral("quit");
    }
    return {};
}

std::optional<Command> commandFromName(const QString& name) {
    const QString n = name.trimmed();
    for (const Command c : {Command::Activate, Command::CheckClipboard, Command::Quit})
        if (n == commandName(c)) return c;
    return std::nullopt;
}

Invocation parseArgs(const QStringList& args) {
    Invocation inv;
    int commands = 0;
    for (const QString& a : args) {
        if (!a.startsWith(QLatin1String("--"))) continue;
        if (a == QLatin1String("--version")) {
            inv.version = true;
        } else if (a == QLatin1String("--check-clipboard")) {
            inv.command = Command::CheckClipboard;
            ++commands;
        } else if (a == QLatin1String("--quit")) {
            inv.command = Command::Quit;
            ++commands;
        } else {
            inv.error = QStringLiteral("opción desconocida: %1").arg(a);
            return inv;
        }
    }
    if (commands > 1) inv.error = QStringLiteral("--check-clipboard y --quit no van juntas");
    return inv;
}

QString defaultInstanceDir() {
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty()) return runtime + QStringLiteral("/revisa-tray");
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
    return QDir::tempPath() + QStringLiteral("/revisa-tray-") + user;
}

SingleInstance::SingleInstance(const QString& dir, QObject* parent) : QObject(parent), dir_(dir) {}

SingleInstance::~SingleInstance() { release(); }

bool SingleInstance::tryPrimary() {
    if (lock_ && lock_->isLocked()) return true;
    if (!QDir().mkpath(dir_)) return true;  // no place for a lock: run anyway, alone
    QFile::setPermissions(dir_, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    lock_ = std::make_unique<QLockFile>(dir_ + QStringLiteral("/instance.lock"));
    lock_->setStaleLockTime(0);  // never stale by age: only when its process is gone
    return lock_->tryLock(0);
}

void SingleInstance::release() {
    if (lock_) lock_->unlock();
    lock_.reset();
}

bool SingleInstance::send(Command c) {
    const QString name = QStringLiteral("%1/%2-%3.cmd")
                             .arg(dir_)
                             .arg(QDateTime::currentMSecsSinceEpoch())
                             .arg(QCoreApplication::applicationPid());
    QSaveFile f(name);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(commandName(c).toUtf8() + '\n');
    return f.commit();
}

void SingleInstance::listen() {
    // Leftovers of an instance that died are dropped; a command written in the last seconds may
    // come from a second instance that raced with our start: it is run.
    QDir dir(dir_);
    const QDateTime recent = QDateTime::currentDateTime().addSecs(-10);
    for (const QFileInfo& f : dir.entryInfoList({QStringLiteral("*.cmd")}, QDir::Files))
        if (f.lastModified() < recent) dir.remove(f.fileName());
    if (watcher_) return;
    watcher_ = new QFileSystemWatcher(this);
    watcher_->addPath(dir_);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { drain(); });
    QTimer::singleShot(0, this, [this] { drain(); });
}

void SingleInstance::drain() {
    // Read and delete every file first, then run them: a command may spin a nested event loop
    // that calls drain() again.
    QDir dir(dir_);
    std::vector<Command> commands;
    for (const QString& entry : dir.entryList({QStringLiteral("*.cmd")}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(entry));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray content = f.readAll();
        f.close();
        dir.remove(entry);
        if (const auto c = commandFromName(QString::fromUtf8(content))) commands.push_back(*c);
        else qWarning("revisa-tray: ignored unknown command file %s", qPrintable(entry));
    }
    for (const Command c : commands) emit received(c);
}

}  // namespace revisa::tray
