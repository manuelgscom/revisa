#pragma once
// «Ajustes»: hotkey, threads, enabled types and start with the session. OK hands the new values
// to the app (applyFn), which registers the hotkey, saves the config and restarts the engine if
// needed; if that fails the dialog shows the reason and stays open.
//
//   [r] Ajustes
//   Atajo          ╭ Atajo de teclado / [Ctrl+Alt+Space      ] Restablecer ╮
//   Correcciones   ╭ the types, two columns, checkable by their Spanish name ╮
//   Rendimiento    ╭ Hilos del procesador                  [-] Automático [+] ╮
//   Sistema        ╭ [ ] Arrancar con el sistema                            ╮
//                                                      Cancelar  [✓ Aceptar]

#include <QDialog>
#include <QString>

#include <functional>

#include "core/config/config.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QToolButton;
class QKeySequenceEdit;
class QListWidget;
class QSpinBox;

namespace revisa::tray {

struct Settings {
    config::Config config;
    bool autostart = false;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // applyFn: returns false and fills error (Spanish) to keep the dialog open.
    using ApplyFn = std::function<bool(const Settings& settings, QString* error)>;

    SettingsDialog(const Settings& current, ApplyFn applyFn, QWidget* parent = nullptr);

    // The values the widgets hold now, on top of the config it was opened with.
    Settings values() const;

    void accept() override;

private:
    void applyTheme();
    void updateStepper();

    Settings initial_;
    ApplyFn apply_;
    QKeySequenceEdit* hotkey_ = nullptr;
    QSpinBox* threads_ = nullptr;
    QListWidget* types_ = nullptr;
    QCheckBox* autostart_ = nullptr;
    QLabel* appIcon_ = nullptr;
    QLabel* keyIcon_ = nullptr;
    QToolButton* minus_ = nullptr;
    QToolButton* plus_ = nullptr;
    QPushButton* ok_ = nullptr;
    bool clickOnBox_ = false;  // the last click on a type already toggled it (on its box)
};

}  // namespace revisa::tray
