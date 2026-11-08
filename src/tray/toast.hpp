#pragma once
// A short notice near the cursor when there is no tray balloon («Sin errores», «Copiado: pega con
// Ctrl+V», errors): the popup's card style as a compact pill, with an icon, a fade-in and a
// fade-out. It never takes the focus and lets the mouse through.
//
//   ╭───────────────────────────────╮
//   │ (✓)  Sin errores              │
//   ╰───────────────────────────────╯

#include <QString>

#include "tray/card_window.hpp"

class QLabel;

namespace revisa::tray {

class Toast : public CardWindow {
    Q_OBJECT

public:
    enum class Kind { Info, Success, Copied, Warning, Error, Busy };

    explicit Toast(QWidget* parent = nullptr);

    // Shows `text` near the cursor (replacing what it showed).
    void showText(const QString& text, Kind kind);
    // Fades out (or hides at once without a compositor).
    void dismiss();
    QString text() const;
    Kind kind() const { return kind_; }

protected:
    void applyTheme() override;

private:
    void refreshIcon();

    QLabel* icon_ = nullptr;
    Spinner* spinner_ = nullptr;
    QLabel* text_ = nullptr;
    Kind kind_ = Kind::Info;
};

}  // namespace revisa::tray
