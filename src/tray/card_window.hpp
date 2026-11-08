#pragma once
// A frameless top-level window that shows one card: the popup and the toast.
//
//   with a compositor                         without one (Xvfb, some bare X11 sessions)
//   ┌ window (translucent) ─────────────┐     ┌ window = card ───────────┐
//   │   ╭ card ───────────────────╮     │     │ opaque, square, 1 px     │
//   │   │ radius 12, 1 px border  │     │     │ border, no margins       │
//   │   ╰─────────────────────────╯     │     └──────────────────────────┘
//   │      soft drop shadow             │
//   └───────────────────────────────────┘
//
// Without a compositor translucent pixels come out black, so the floating look (rounded corners,
// shadow, fade) is only used when the probe says a compositor is running. The probe is asked
// again before every show; if the answer changed, the native window is recreated.

#include <QFrame>
#include <QMargins>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include <functional>

class QGraphicsDropShadowEffect;
class QPropertyAnimation;
class QVBoxLayout;

namespace revisa::tray {

class CardWindow : public QWidget {
    Q_OBJECT

public:
    explicit CardWindow(Qt::WindowFlags flags, QWidget* parent = nullptr);

    QFrame* card() const { return card_; }
    // Is a compositor running? Asked before every show; unset = no (the safe, opaque look).
    void setCompositorProbe(std::function<bool()> probe) { probe_ = std::move(probe); }
    bool floating() const { return floating_; }

    // Asks the probe and switches between the floating and the opaque look (before a show).
    void updateCompositing();
    // Moves the window so the card's top-left corner sits at cursor + (12, 16), inside the
    // available geometry of the cursor's screen.
    void placeNear(const QPoint& cursor);
    // show() with a short fade-in when floating.
    void showFaded();
    // hide() after a short fade-out when floating.
    void hideFaded();

protected:
    // Re-applies the stylesheet and whatever depends on the colors (called on theme changes).
    virtual void applyTheme();

private:
    QFrame* card_ = nullptr;
    QVBoxLayout* outer_ = nullptr;
    QGraphicsDropShadowEffect* shadow_ = nullptr;
    QPropertyAnimation* fade_ = nullptr;
    std::function<bool()> probe_;
    bool floating_ = false;
    bool fadingOut_ = false;
};

// A small indeterminate progress indicator: an arc that turns, painted in the accent color. It
// only animates while visible.
class Spinner : public QWidget {
    Q_OBJECT

public:
    explicit Spinner(QWidget* parent = nullptr, int size = 16);
    QSize sizeHint() const override { return {size_, size_}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QTimer timer_;
    int size_;
    int angle_ = 0;
};

}  // namespace revisa::tray
