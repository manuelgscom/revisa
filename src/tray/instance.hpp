#pragma once
// Command line and single instance of revisa-tray.
//
// Only Qt Core (the app uses Core, Gui and Widgets only, no Network module): the first instance
// holds a QLockFile in a per-user runtime directory; a second instance drops its command there
// as a small file (written atomically with QSaveFile) and exits; the first one watches the
// directory (QFileSystemWatcher), runs each command and deletes its file.
//
//   revisa-tray                     start, or tell the running instance to say it is running
//   revisa-tray --check-clipboard   check the clipboard (Wayland: bind it to a desktop shortcut)
//   revisa-tray --quit              ask the running instance to quit
//   revisa-tray --version

#include <QFileSystemWatcher>
#include <QLockFile>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace revisa::tray {

enum class Command { Activate, CheckClipboard, Quit };

QString commandName(Command c);                          // "activate", "check-clipboard", "quit"
std::optional<Command> commandFromName(const QString& name);  // trimmed; nullopt if unknown

struct Invocation {
    Command command = Command::Activate;
    bool version = false;
    QString error;  // Spanish; non-empty = bad command line
};

// args without argv[0]. Our flags start with "--"; any other "--…" is an error. Arguments that
// do not start with "--" are left to Qt (-platform offscreen…) and ignored here.
Invocation parseArgs(const QStringList& args);

// Default directory of the lock and the commands: <runtime dir>/revisa-tray, else
// <temp>/revisa-tray-<user>.
QString defaultInstanceDir();

class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(const QString& dir, QObject* parent = nullptr);
    ~SingleInstance() override;

    // true if this process is now the only instance (it holds the lock until destroyed or
    // release()). Stale locks of dead processes are taken over.
    bool tryPrimary();
    void release();

    // From a second instance: leaves `c` for the primary one. false if it could not be written.
    bool send(Command c);

    // Primary only: starts watching for commands (leftovers older than 10 s are deleted first).
    void listen();

signals:
    void received(revisa::tray::Command c);

private:
    void drain();

    QString dir_;
    std::unique_ptr<QLockFile> lock_;
    QFileSystemWatcher* watcher_ = nullptr;
};

}  // namespace revisa::tray
