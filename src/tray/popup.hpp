#pragma once
// The small window next to the cursor with the corrections:
//
//   ╭──────────────────────────────────────────────────────────────╮
//   │ [r] revisa  3 correcciones                               (×) │  header
//   │ ┌──────────────────────────────────────────────────────────┐ │
//   │ │ Hola Ana, ayer ~~e echo~~ he hecho la compra y ~~despues~~ │ │  diff of the whole text
//   │ │ después fuimos al cine.                                  │ │
//   │ └──────────────────────────────────────────────────────────┘ │
//   │ [✓] ~~e~~ → he                                  (He / e, …) │  one row per edit: the whole
//   │     Aquí va «he», del verbo «haber»…                         │  row toggles it
//   │ [ ] ~~despues~~ → después                        (Falta tilde) │  unchecked rows are dimmed
//   │──────────────────────────────────────────────────────────────│
//   │ [↵] Aplicar  [Esc] Cancelar           Cancelar  [✓ Aplicar]  │  footer
//   ╰──────────────────────────────────────────────────────────────╯
//
// Two states: a compact card with a spinner and a message («Leyendo el portapapeles…»,
// «Corrigiendo…») and the result. Enter applies (accepted(text) with the checked edits), Esc or ×
// cancels. Losing the focus does not close it: some window managers steal the focus for a moment.
// The card floats (rounded, shadow, fade-in) only with a compositor: see CardWindow.

#include <QCheckBox>
#include <QString>

#include <vector>

#include "core/check/check.hpp"
#include "tray/card_window.hpp"

class QKeyEvent;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace revisa::tray {

class Popup : public CardWindow {
    Q_OBJECT

public:
    explicit Popup(QWidget* parent = nullptr);

    // Shows only `message` with a spinner, near the cursor, and takes the focus.
    void showMessage(const QString& message);
    // Shows the diff and one row per edit (all checked), near the cursor, and takes the focus.
    void showResult(const QString& original, const std::vector<check::Edit>& edits);

    bool hasResult() const { return hasResult_; }
    std::vector<bool> enabled() const;
    // original with the checked edits applied.
    QString selectedText() const;
    // The rich text of the diff label as it is now (tests).
    QString diffText() const;
    // One per edit: the row (a QCheckBox whose text is editLabel()).
    const std::vector<QCheckBox*>& checkBoxes() const { return boxes_; }

    void apply();   // Enter
    void cancel();  // Esc, ×

signals:
    void accepted(const QString& text);
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    // A close from the window manager (Alt+F4) is a cancel: the app hides us and ends the flow.
    void closeEvent(QCloseEvent* event) override;
    void applyTheme() override;

private:
    void clearEdits();
    void refreshDiff();
    void placeAndShow();
    void fitScrollHeight();

    QLabel* icon_ = nullptr;
    QLabel* status_ = nullptr;
    Spinner* spinner_ = nullptr;
    QToolButton* close_ = nullptr;
    QWidget* body_ = nullptr;
    QLabel* diff_ = nullptr;
    QWidget* editsBox_ = nullptr;
    QVBoxLayout* editsLayout_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* footer_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    std::vector<QCheckBox*> boxes_;
    QString original_;
    std::vector<check::Edit> edits_;
    bool hasResult_ = false;
};

}  // namespace revisa::tray
