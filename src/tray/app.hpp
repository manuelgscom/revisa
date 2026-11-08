#pragma once
// revisa-tray: the tray icon, its menu and the two flows.
//
//   hotkey ─► w = activeWindow() ─► readSelection(w) ─► EngineWorker ─► Popup ─Enter─► paste(w)
//   menu / --check-clipboard / no simulated input:
//            Popup «Leyendo…» ─► readSelection(0) ─► EngineWorker ─► Popup ─Enter─► clipboard
//
// One flow at a time: a hotkey press or a clipboard check while another one is running is
// ignored. Every key event goes to the log as "revisa-tray: …" (the e2e waits on those lines).

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>

#include <memory>

#include "core/check/check.hpp"
#include "core/config/config.hpp"
#include "tray/instance.hpp"
#include "tray/platform/platform.hpp"
#include "tray/toast.hpp"

namespace revisa::tray {

class EngineWorker;
class ModelSetup;
class Popup;
class SettingsDialog;
struct Settings;

class TrayApp : public QObject {
    Q_OBJECT

public:
    TrayApp();
    ~TrayApp() override;

    // Loads the config, registers the hotkey and finds/loads the model.
    void start();

    // A command from the command line or from a second instance.
    void run(Command c);

    // «Corregir el portapapeles».
    void checkClipboard();

    // A short message: the tray balloon if there is a tray, else a toast near the cursor. `kind`
    // picks the toast's icon (and the balloon's).
    void notify(const QString& text, int ms = 2500, Toast::Kind kind = Toast::Kind::Info);

private:
    enum class EngineState { Loading, Downloading, Ready, Failed };

    void onHotkey();
    void readClipboard(quint64 seq, int triesLeft);
    void submit(const QString& text);
    void onFinished(quint64 id, const check::Result& result);
    void onAccepted(const QString& text);
    void endFlow();

    void setStatus(EngineState state, const QString& detail = {});
    void startEngine(const QString& modelPath);
    void onModelFailed(const QString& message);
    void notifyNotReady();
    void showToast(const QString& text, int ms, Toast::Kind kind);
    void refreshLook();  // menu icons and stylesheet for the theme and state in use
    bool registerHotkey(const std::string& hotkey, QString* error);
    void openSettings();
    bool applySettings(const Settings& s, QString* error);
    void showAbout();
    check::Options checkOptions() const;

    config::Config config_;
    std::unique_ptr<Platform> platform_;
    EngineWorker* worker_ = nullptr;  // lives on thread_
    QThread thread_;
    ModelSetup* setup_ = nullptr;
    Popup* popup_ = nullptr;
    QPointer<SettingsDialog> settings_;
    Toast* toast_ = nullptr;
    QTimer toastTimer_;
    QTimer busyTimer_;  // shows «Corrigiendo…» when a check takes a while

    QSystemTrayIcon* tray_ = nullptr;
    QMenu* menu_ = nullptr;
    QAction* status_ = nullptr;
    QAction* hotkeyStatus_ = nullptr;
    QAction* downloadAction_ = nullptr;
    QAction* pauseAction_ = nullptr;

    EngineState state_ = EngineState::Loading;
    QString stateDetail_;
    QString modelPath_;
    int generation_ = 0;
    bool paused_ = false;
    bool pendingClipboard_ = false;  // a clipboard check asked for while the model was loading

    // The flow in progress.
    bool busy_ = false;
    bool clipboardFlow_ = false;
    WindowRef window_ = 0;
    QString original_;
    quint64 pendingId_ = 0;
    quint64 lastId_ = 0;
    quint64 flowSeq_ = 0;  // bumped by every clipboard check and endFlow: stale timers give up
};

}  // namespace revisa::tray
