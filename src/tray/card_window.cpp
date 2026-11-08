#include "tray/card_window.hpp"

#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

#include "tray/theme.hpp"

namespace revisa::tray {

namespace {

// Room around the card for the shadow (blur 28, 8 px down).
constexpr QMargins kShadowMargins(24, 16, 24, 32);
constexpr int kFadeInMs = 120;
constexpr int kFadeOutMs = 150;

void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

}  // namespace

CardWindow::CardWindow(Qt::WindowFlags flags, QWidget* parent)
    : QWidget(parent, flags | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    outer_ = new QVBoxLayout(this);
    outer_->setContentsMargins(0, 0, 0, 0);
    outer_->setSizeConstraint(QLayout::SetFixedSize);
    card_ = new QFrame(this);
    card_->setObjectName(QStringLiteral("card"));
    card_->setProperty("floating", false);
    outer_->addWidget(card_);

    fade_ = new QPropertyAnimation(this, "windowOpacity", this);
    connect(fade_, &QPropertyAnimation::finished, this, [this] {
        if (!fadingOut_) return;
        fadingOut_ = false;
        hide();
        setWindowOpacity(1.0);
    });
    connect(theme::notifier(), &theme::Notifier::changed, this, [this] { applyTheme(); });
}

void CardWindow::applyTheme() {
    setStyleSheet(theme::styleSheet(theme::colors()));
    if (shadow_) shadow_->setColor(theme::colors().shadow);
}

void CardWindow::updateCompositing() {
    const bool want = probe_ ? probe_() : false;
    if (want == floating_) return;
    if (testAttribute(Qt::WA_WState_Created)) {
        // The visual of a native window is fixed when it is created: start over.
        hide();
        destroy();
    }
    floating_ = want;
    setAttribute(Qt::WA_TranslucentBackground, floating_);
    outer_->setContentsMargins(floating_ ? kShadowMargins : QMargins());
    card_->setProperty("floating", floating_);
    repolish(card_);
    if (floating_) {
        shadow_ = new QGraphicsDropShadowEffect(card_);
        shadow_->setBlurRadius(28);
        shadow_->setOffset(0, 8);
        shadow_->setColor(theme::colors().shadow);
        card_->setGraphicsEffect(shadow_);  // owns it
    } else {
        card_->setGraphicsEffect(nullptr);  // deletes it
        shadow_ = nullptr;
    }
}

void CardWindow::placeNear(const QPoint& cursor) {
    adjustSize();
    const QMargins m = outer_->contentsMargins();
    const QSize cardSize = card_->sizeHint().expandedTo(card_->size());
    const QScreen* screen = QGuiApplication::screenAt(cursor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    QPoint at = cursor + QPoint(12, 16);
    if (screen) {
        const QRect area = screen->availableGeometry().adjusted(8, 8, -8, -8);
        at.setX(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() + 1 - cardSize.width())));
        at.setY(std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() + 1 - cardSize.height())));
    }
    move(at - QPoint(m.left(), m.top()));
}

void CardWindow::showFaded() {
    fade_->stop();
    fadingOut_ = false;
    if (!floating_ || isVisible()) {
        setWindowOpacity(1.0);
        show();
        return;
    }
    setWindowOpacity(0.0);
    show();
    fade_->setDuration(kFadeInMs);
    fade_->setStartValue(0.0);
    fade_->setEndValue(1.0);
    fade_->setEasingCurve(QEasingCurve::OutCubic);
    fade_->start();
}

void CardWindow::hideFaded() {
    if (!isVisible()) return;
    if (!floating_) {
        hide();
        return;
    }
    fade_->stop();
    fadingOut_ = true;
    fade_->setDuration(kFadeOutMs);
    fade_->setStartValue(windowOpacity());
    fade_->setEndValue(0.0);
    fade_->setEasingCurve(QEasingCurve::InCubic);
    fade_->start();
}

// --- Spinner -------------------------------------------------------------------------------------

Spinner::Spinner(QWidget* parent, int size) : QWidget(parent), size_(size) {
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 12) % 360;
        update();
    });
}

void Spinner::paintEvent(QPaintEvent* /*event*/) {
    const theme::Colors& c = theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal pen = 2.0;
    const QRectF box = QRectF(rect()).adjusted(pen, pen, -pen, -pen);
    QColor track = c.accent;
    track.setAlphaF(0.18f);
    p.setPen(QPen(track, pen));
    p.drawEllipse(box);
    p.setPen(QPen(c.accent, pen, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(box, -angle_ * 16, 100 * 16);
}

void Spinner::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    timer_.start();
}

void Spinner::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer_.stop();
}

}  // namespace revisa::tray
