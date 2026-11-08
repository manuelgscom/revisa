#include "tray/app.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>

#include "core/fetch/model_resolve.hpp"
#include "tray/diff_html.hpp"
#include "tray/engine_worker.hpp"
#include "tray/icons.hpp"
#include "tray/model_setup.hpp"
#include "tray/popup.hpp"
#include "tray/settings_dialog.hpp"
#include "tray/theme.hpp"

namespace revisa::tray {

namespace {

const QString kCopied = QStringLiteral("Copiado: pega con Ctrl+V");
const QString kChecking = QStringLiteral("Corrigiendo…");

#if REVISA_TEST_HOOKS
// A non-negative number from the environment, or nullopt (unset or not a number).
std::optional<double> envNumber(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return std::nullopt;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    if (end == v || *end != '\0' || !(d >= 0)) return std::nullopt;
    return d;
}
#endif

QString fromStd(const std::string& s) { return QString::fromStdString(s); }

}  // namespace

TrayApp::TrayApp() {
    platform_ = makePlatform();
    qInfo("revisa-tray: platform %s", qPrintable(platform_->name()));
    connect(platform_.get(), &Platform::hotkeyPressed, this, &TrayApp::onHotkey);

    worker_ = new EngineWorker();
    worker_->moveToThread(&thread_);
    connect(worker_, &EngineWorker::ready, this, [this](int generation) {
        if (generation != generation_) return;
        qInfo("revisa-tray: engine ready");
        setStatus(EngineState::Ready);
        if (pendingClipboard_) {
            pendingClipboard_ = false;
            checkClipboard();
        }
    });
    connect(worker_, &EngineWorker::failed, this, [this](int generation, const QString& message) {
        if (generation != generation_) return;
        qInfo("revisa-tray: engine failed: %s", qPrintable(message));
        pendingClipboard_ = false;
        setStatus(EngineState::Failed, message);
        notify(QStringLiteral("Error: %1").arg(message), 6000, Toast::Kind::Error);
    });
    connect(worker_, &EngineWorker::finished, this, &TrayApp::onFinished);
    thread_.setObjectName(QStringLiteral("revisa-engine"));
    thread_.start();

    setup_ = new ModelSetup(this);
    connect(setup_, &ModelSetup::modelReady, this, &TrayApp::startEngine);
    connect(setup_, &ModelSetup::failed, this, &TrayApp::onModelFailed);
    connect(setup_, &ModelSetup::progress, this,
            [this](int percent) { setStatus(EngineState::Downloading, QString::number(percent)); });

    const auto compositor = [p = platform_.get()] { return p->hasCompositor(); };
    popup_ = new Popup();
    popup_->setCompositorProbe(compositor);
    connect(popup_, &Popup::accepted, this, &TrayApp::onAccepted);
    connect(popup_, &Popup::cancelled, this, [this] {
        qInfo("revisa-tray: cancelled");
        popup_->hide();
        if (!clipboardFlow_ && window_) platform_->activate(window_);
        endFlow();
    });

    toast_ = new Toast();
    toast_->setCompositorProbe(compositor);
    toastTimer_.setSingleShot(true);
    connect(&toastTimer_, &QTimer::timeout, toast_, &Toast::dismiss);
    busyTimer_.setSingleShot(true);
    busyTimer_.setInterval(400);
    connect(&busyTimer_, &QTimer::timeout, this, [this] {
        if (busy_ && pendingId_ != 0 && !clipboardFlow_) showToast(kChecking, 60000, Toast::Kind::Busy);
    });

    menu_ = new QMenu();
    menu_->setObjectName(QStringLiteral("revisa-menu"));
    status_ = menu_->addAction(QString());
    status_->setEnabled(false);
    hotkeyStatus_ = menu_->addAction(QStringLiteral("Atajo no disponible: elige otro en Ajustes"));
    hotkeyStatus_->setEnabled(false);
    hotkeyStatus_->setVisible(false);
    menu_->addSeparator();
    menu_->addAction(QStringLiteral("Corregir el portapapeles"), this, &TrayApp::checkClipboard);
    downloadAction_ = menu_->addAction(QStringLiteral("Descargar el modelo"), this, [this] { setup_->download(); });
    downloadAction_->setVisible(false);
    pauseAction_ = menu_->addAction(QStringLiteral("Pausar"), this, [this] {
        paused_ = !paused_;
        pauseAction_->setText(paused_ ? QStringLiteral("Reanudar") : QStringLiteral("Pausar"));
        refreshLook();
        qInfo("revisa-tray: %s", paused_ ? "paused" : "resumed");
    });
    menu_->addAction(QStringLiteral("Ajustes…"), this, &TrayApp::openSettings);
    menu_->addAction(QStringLiteral("Acerca de…"), this, &TrayApp::showAbout);
    menu_->addSeparator();
    menu_->addAction(QStringLiteral("Salir"), qApp, &QCoreApplication::quit);

    tray_ = new QSystemTrayIcon(QGuiApplication::windowIcon(), this);
    tray_->setContextMenu(menu_);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) menu_->popup(QCursor::pos());
    });
    tray_->show();
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        qWarning("revisa-tray: no system tray; the hotkey and --check-clipboard still work");

    connect(theme::notifier(), &theme::Notifier::changed, this, &TrayApp::refreshLook);
    setStatus(EngineState::Loading);
}

TrayApp::~TrayApp() {
    platform_->unregisterHotkey();
    thread_.quit();
    thread_.wait();
    delete worker_;  // its thread has finished
    delete settings_;
    delete popup_;
    delete toast_;
    tray_->hide();
    tray_->setContextMenu(nullptr);
    delete menu_;
}

void TrayApp::start() {
    const config::ConfigResult loaded = config::load_config();
    if (loaded.config) {
        config_ = *loaded.config;
    } else {
        qWarning("revisa-tray: config error: %s", loaded.error.c_str());
        notify(QStringLiteral("Error en la configuración: %1").arg(fromStd(loaded.error)), 6000, Toast::Kind::Error);
    }

    if (platform_->canSimulateInput()) {
        QString error;
        if (!registerHotkey(config_.hotkey, &error)) {
            notify(error, 6000, Toast::Kind::Warning);
            QTimer::singleShot(0, this, &TrayApp::openSettings);
        }
    } else {
        qInfo("revisa-tray: clipboard-only mode, no global hotkey");
    }

    if (fakeEngineRequested()) startEngine(QString());
    else setup_->run(config_.model);
}

void TrayApp::run(Command c) {
    switch (c) {
        case Command::Activate:
            notify(QStringLiteral("revisa ya está en marcha"));
            return;
        case Command::CheckClipboard:
            checkClipboard();
            return;
        case Command::Quit:
            qInfo("revisa-tray: quit");
            QCoreApplication::quit();
            return;
    }
}

// --- Flows ------------------------------------------------------------------------------------

void TrayApp::onHotkey() {
    if (paused_) {
        qInfo("revisa-tray: hotkey ignored (paused)");
        return;
    }
    if (busy_ || popup_->isVisible()) {
        qInfo("revisa-tray: hotkey ignored (busy)");
        return;
    }
    if (!platform_->canSimulateInput()) {
        checkClipboard();
        return;
    }
    if (state_ != EngineState::Ready) {
        notifyNotReady();
        return;
    }
    busy_ = true;  // before any platform wait: those spin nested event loops
    clipboardFlow_ = false;
    window_ = platform_->activeWindow();
    qInfo("revisa-tray: hotkey pressed (window 0x%llx)", static_cast<unsigned long long>(window_));
    const std::optional<QString> text = platform_->readSelection(window_);
    if (!text || text->trimmed().isEmpty()) {
        qInfo("revisa-tray: no selection");
        notify(QStringLiteral("No hay texto seleccionado"), 2500, Toast::Kind::Warning);
        endFlow();
        return;
    }
    submit(*text);
}

void TrayApp::checkClipboard() {
    if (busy_ || popup_->isVisible()) {
        qInfo("revisa-tray: clipboard check ignored (busy)");
        return;
    }
    if (state_ == EngineState::Loading || state_ == EngineState::Downloading) {
        pendingClipboard_ = true;
        notify(QStringLiteral("Aún cargando el modelo: corregiré el portapapeles al terminar"));
        return;
    }
    if (state_ == EngineState::Failed) {
        notifyNotReady();
        return;
    }
    busy_ = true;
    clipboardFlow_ = true;
    window_ = 0;
    qInfo("revisa-tray: clipboard check");
    // Wayland only hands the clipboard to a focused window: show the popup first.
    popup_->showMessage(QStringLiteral("Leyendo el portapapeles…"));
    readClipboard(++flowSeq_, 10);
}

void TrayApp::readClipboard(quint64 seq, int triesLeft) {
    if (seq != flowSeq_ || !busy_ || !clipboardFlow_) return;  // cancelled meanwhile
    if (!popup_->isActiveWindow() && triesLeft > 0) {
        QTimer::singleShot(50, this, [this, seq, triesLeft] { readClipboard(seq, triesLeft - 1); });
        return;
    }
    // CLIPBOARD on every platform, never platform_->readSelection(): on X11 that reads PRIMARY
    // (or simulates Ctrl+C into our own popup).
    const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    if (text.trimmed().isEmpty()) {
        qInfo("revisa-tray: no selection");
        popup_->hide();
        notify(QStringLiteral("El portapapeles no tiene texto"), 2500, Toast::Kind::Warning);
        endFlow();
        return;
    }
    submit(text);
}

void TrayApp::submit(const QString& text) {
    if (codePoints(text) > kMaxTextChars) {
        qInfo("revisa-tray: text too long (%zu code points)", codePoints(text));
        popup_->hide();
        notify(QStringLiteral("Texto demasiado largo (máx. 20.000 caracteres)"), 2500, Toast::Kind::Warning);
        endFlow();
        return;
    }
    original_ = text;
    pendingId_ = ++lastId_;
    qInfo("revisa-tray: checking %lld chars", static_cast<long long>(text.size()));
    if (clipboardFlow_) popup_->showMessage(kChecking);
    else busyTimer_.start();
    const quint64 id = pendingId_;
    const check::Options options = checkOptions();
    EngineWorker* worker = worker_;
    QMetaObject::invokeMethod(worker, [worker, id, text, options] { worker->check(id, text, options); },
                              Qt::QueuedConnection);
}

void TrayApp::onFinished(quint64 id, const check::Result& result) {
    if (id == 0 || id != pendingId_) return;  // cancelled or stale
    pendingId_ = 0;
    busyTimer_.stop();
    if (toast_->text() == kChecking) toast_->hide();
    if (!result.error.empty()) {
        const QString message = engineErrorText(result);
        qInfo("revisa-tray: check failed: %s", qPrintable(message));
        popup_->hide();
        notify(QStringLiteral("Error: %1").arg(message), 6000, Toast::Kind::Error);
        endFlow();
        return;
    }
    if (result.edits.empty()) {
        qInfo("revisa-tray: no edits");
        popup_->hide();
        notify(QStringLiteral("Sin errores"), 2500, Toast::Kind::Success);
        endFlow();
        return;
    }
    popup_->showResult(original_, result.edits);
    qInfo("revisa-tray: popup shown (%zu edits)", result.edits.size());
}

void TrayApp::onAccepted(const QString& text) {
    popup_->hide();  // before paste: the target window must get the focus back
    if (text == original_) {
        qInfo("revisa-tray: nothing to apply");
        if (!clipboardFlow_ && window_) platform_->activate(window_);
    } else if (clipboardFlow_) {
        platform_->paste(0, text);
        qInfo("revisa-tray: copied to clipboard");
        notify(kCopied, 4000, Toast::Kind::Copied);
    } else if (platform_->paste(window_, text)) {
        qInfo("revisa-tray: pasted");
    } else {
        qInfo("revisa-tray: paste failed, text left in clipboard");
        notify(kCopied, 4000, Toast::Kind::Copied);
    }
    endFlow();
}

void TrayApp::endFlow() {
    busy_ = false;
    clipboardFlow_ = false;
    window_ = 0;
    original_.clear();
    pendingId_ = 0;
    ++flowSeq_;
    busyTimer_.stop();
    if (toast_->text() == kChecking) toast_->hide();
}

check::Options TrayApp::checkOptions() const {
    check::Options options;
    options.disabled = config_.disabled_types;
#if REVISA_TEST_HOOKS
    if (const auto r = envNumber("REVISA_GUARD_MAX_CHANGE_RATIO")) options.thresholds.max_change_ratio = *r;
    if (const auto n = envNumber("REVISA_GUARD_MAX_OTRO")) options.thresholds.max_otro = static_cast<int>(*n);
#endif
    return options;
}

// --- Engine and model -------------------------------------------------------------------------

void TrayApp::startEngine(const QString& modelPath) {
    modelPath_ = modelPath;
    const int generation = ++generation_;
    const int threads = config_.threads;
    setStatus(EngineState::Loading);
    qInfo("revisa-tray: loading the engine (%s)", modelPath.isEmpty() ? "fake" : qPrintable(modelPath));
    EngineWorker* worker = worker_;
    QMetaObject::invokeMethod(worker, [worker, generation, modelPath, threads] {
        worker->start(generation, modelPath, threads);
    }, Qt::QueuedConnection);
}

void TrayApp::onModelFailed(const QString& message) {
    qInfo("revisa-tray: model unavailable: %s", qPrintable(message));
    pendingClipboard_ = false;  // a later download must not fire a forgotten check
    setStatus(EngineState::Failed, message);
    notify(QStringLiteral("Error: %1").arg(message), 6000, Toast::Kind::Error);
}

void TrayApp::setStatus(EngineState state, const QString& detail) {
    state_ = state;
    stateDetail_ = detail;
    QString text;
    switch (state) {
        case EngineState::Loading: text = QStringLiteral("Cargando modelo…"); break;
        case EngineState::Downloading: text = QStringLiteral("Descargando modelo… %1 %").arg(detail); break;
        case EngineState::Ready: text = QStringLiteral("Listo"); break;
        case EngineState::Failed: text = QStringLiteral("Error: %1").arg(detail); break;
    }
    status_->setText(text);
    tray_->setToolTip(QStringLiteral("revisa: %1").arg(text));
    downloadAction_->setVisible(state == EngineState::Failed && setup_->retryAvailable());
    refreshLook();
}

void TrayApp::refreshLook() {
    const theme::Colors& c = theme::colors();
    const int scheme = static_cast<int>(theme::scheme());
    if (menu_->property("revisaScheme") != scheme) {  // status changes are frequent while downloading
        menu_->setProperty("revisaScheme", scheme);
        menu_->setStyleSheet(theme::styleSheet(c));
    }
    QColor dot = c.textTertiary;  // loading
    switch (state_) {
        case EngineState::Loading: break;
        case EngineState::Downloading: dot = c.accent; break;
        case EngineState::Ready: dot = paused_ ? c.warning : c.success; break;
        case EngineState::Failed: dot = c.danger; break;
    }
    status_->setIcon(icons::dot(dot));
    hotkeyStatus_->setIcon(icons::icon(icons::Glyph::Warning, c.warning));
    const QColor ink = c.textSecondary;
    for (QAction* a : menu_->actions()) {
        const QString t = a->text();
        if (t == QStringLiteral("Corregir el portapapeles")) a->setIcon(icons::icon(icons::Glyph::Copy, ink));
        else if (t == QStringLiteral("Ajustes…")) a->setIcon(icons::icon(icons::Glyph::Settings, ink));
        else if (t == QStringLiteral("Acerca de…")) a->setIcon(icons::icon(icons::Glyph::Info, ink));
        else if (t == QStringLiteral("Salir")) a->setIcon(icons::icon(icons::Glyph::Power, ink));
    }
    downloadAction_->setIcon(icons::icon(icons::Glyph::Download, ink));
    pauseAction_->setIcon(icons::icon(paused_ ? icons::Glyph::Play : icons::Glyph::Pause, ink));
}

void TrayApp::notifyNotReady() {
    switch (state_) {
        case EngineState::Loading: notify(QStringLiteral("Aún cargando el modelo")); return;
        case EngineState::Downloading: notify(QStringLiteral("Aún descargando el modelo")); return;
        case EngineState::Failed:
            notify(QStringLiteral("Error: %1").arg(stateDetail_), 6000, Toast::Kind::Error);
            return;
        case EngineState::Ready: return;
    }
}

// --- Hotkey and settings ----------------------------------------------------------------------

bool TrayApp::registerHotkey(const std::string& hotkey, QString* error) {
    const QString text = fromStd(hotkey);
    const QKeySequence seq = QKeySequence::fromString(text, QKeySequence::PortableText);
    bool ok = false;
    if (seq.isEmpty() || seq.count() != 1 || seq[0].key() == Qt::Key_unknown) {
        *error = QStringLiteral("El atajo «%1» no es válido: elige otro en Ajustes.").arg(text);
    } else {
        ok = platform_->registerHotkey(QKeySequence(seq[0]), error);
    }
    if (ok) qInfo("revisa-tray: hotkey %s registered", qPrintable(seq.toString(QKeySequence::PortableText)));
    else qInfo("revisa-tray: hotkey refused: %s", qPrintable(*error));
    hotkeyStatus_->setVisible(!ok);
    return ok;
}

void TrayApp::openSettings() {
    if (settings_) {
        settings_->show();
        settings_->raise();
        settings_->activateWindow();
        return;
    }
    // Free the hotkey while the dialog is open, so the user can type it in the editor.
    platform_->unregisterHotkey();
    settings_ = new SettingsDialog(Settings{config_, autostartEnabled()},
                                   [this](const Settings& s, QString* error) { return applySettings(s, error); });
    settings_->setAttribute(Qt::WA_DeleteOnClose);
    connect(settings_, &QDialog::finished, this, [this](int result) {
        if (result == QDialog::Accepted || !platform_->canSimulateInput()) return;
        QString error;
        if (!registerHotkey(config_.hotkey, &error)) notify(error, 6000, Toast::Kind::Warning);
    });
    qInfo("revisa-tray: settings opened");
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

bool TrayApp::applySettings(const Settings& s, QString* error) {
    if (platform_->canSimulateInput() && !registerHotkey(s.config.hotkey, error)) return false;
    const std::string path = config::config_path();
    if (path.empty()) {
        *error = QStringLiteral("No se encuentra dónde guardar la configuración (falta la carpeta del usuario).");
        return false;
    }
    // Start from the file as it is now, so keys edited by hand since startup survive; the dialog
    // only owns these three. (If the file does not parse, save_config refuses to write.)
    const config::ConfigResult current = config::load_config();
    config::Config next = current.config ? *current.config : config_;
    next.hotkey = s.config.hotkey;
    next.threads = s.config.threads;
    next.disabled_types = s.config.disabled_types;
    std::string saveError;
    if (!config::save_config(path, next, &saveError)) {
        *error = QStringLiteral("No se pudo guardar la configuración: %1").arg(fromStd(saveError));
        return false;
    }
    // Saved: from here on everything applies, and autostart only warns if it fails.
    const bool restart = next.threads != config_.threads && state_ != EngineState::Downloading &&
                         (!modelPath_.isEmpty() || fakeEngineRequested()) && generation_ > 0;
    config_ = next;
    qInfo("revisa-tray: settings saved to %s", path.c_str());
    if (restart) startEngine(modelPath_);
    QString autostartError;
    if (s.autostart != autostartEnabled() && !setAutostart(s.autostart, &autostartError)) {
        qInfo("revisa-tray: autostart not changed: %s", qPrintable(autostartError));
        notify(autostartError, 6000, Toast::Kind::Warning);
    }
    return true;
}

void TrayApp::showAbout() {
    const fetch::ModelSpec spec = fetch::default_model_spec();
    const QString model = fakeEngineRequested() ? QStringLiteral("motor falso (pruebas)")
                                                : fromStd(fetch::model_name(config_.model, spec));
    const theme::Colors& c = theme::colors();
    const QString muted = theme::css(c.textSecondary);
    QString rows = QStringLiteral("<tr><td style=\"color:%1;padding-right:12px\">Modelo</td><td>%2</td></tr>")
                       .arg(muted, model.toHtmlEscaped());
    if (config_.model.empty() && !fakeEngineRequested()) {
        const QString sha = fromStd(spec.sha256);
        rows += QStringLiteral("<tr><td style=\"color:%1;padding-right:12px\">SHA-256</td><td><tt>%2…</tt></td></tr>")
                    .arg(muted, sha.left(16));
    }
    rows += QStringLiteral("<tr><td style=\"color:%1;padding-right:12px\">Qt</td><td>%2 · plataforma %3</td></tr>")
                .arg(muted, QString::fromLatin1(qVersion()), platform_->name());

    QMessageBox box;
    box.setObjectName(QStringLiteral("revisa-about"));
    box.setWindowTitle(QStringLiteral("Acerca de revisa"));
    box.setStyleSheet(theme::styleSheet(c));
    box.setIconPixmap(icons::appIcon(64, box.devicePixelRatioF()));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<p style=\"font-size:large\"><b>revisa %1</b></p>"
                               "<p>Corrector de castellano que funciona sin conexión.</p>")
                    .arg(QStringLiteral(REVISA_VERSION)));
    box.setInformativeText(
        QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\">%1</table>"
                       "<p style=\"color:%2\">Licencia MIT. Qt se usa bajo la LGPLv3 con enlace dinámico; "
                       "el resto de licencias, en THIRD_PARTY_NOTICES.</p>")
            .arg(rows, muted));
    QPushButton* close = box.addButton(QStringLiteral("Cerrar"), QMessageBox::AcceptRole);
    close->setProperty("variant", QStringLiteral("primary"));
    close->setMinimumWidth(96);
    box.setDefaultButton(close);
    box.exec();
}

// --- Notifications ----------------------------------------------------------------------------

void TrayApp::notify(const QString& text, int ms, Toast::Kind kind) {
    qInfo("revisa-tray: notify: %s", qPrintable(text));
    if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages() && tray_->isVisible()) {
        tray_->showMessage(QStringLiteral("revisa"), text, QGuiApplication::windowIcon(), ms);
        return;
    }
    showToast(text, ms, kind);
}

void TrayApp::showToast(const QString& text, int ms, Toast::Kind kind) {
    toast_->showText(text, kind);
    toastTimer_.start(ms);
}

}  // namespace revisa::tray
