#include "tray/toast.hpp"

#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>

#include "tray/icons.hpp"
#include "tray/theme.hpp"

namespace revisa::tray {

namespace {
constexpr int kMaxTextWidth = 380;
}  // namespace

Toast::Toast(QWidget* parent) : CardWindow(Qt::ToolTip | Qt::WindowStaysOnTopHint, parent) {
    setObjectName(QStringLiteral("revisa-toast"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    auto* row = new QHBoxLayout(card());
    row->setContentsMargins(13, 9, 17, 9);
    row->setSpacing(10);
    icon_ = new QLabel(card());
    icon_->setFixedSize(20, 20);
    spinner_ = new Spinner(card(), 18);
    spinner_->hide();
    text_ = new QLabel(card());
    text_->setFont(theme::baseFont());
    text_->setTextFormat(Qt::PlainText);
    row->addWidget(icon_, 0, Qt::AlignVCenter);
    row->addWidget(spinner_, 0, Qt::AlignVCenter);
    row->addWidget(text_, 1, Qt::AlignVCenter);
    applyTheme();
}

void Toast::applyTheme() {
    CardWindow::applyTheme();
    refreshIcon();
}

void Toast::refreshIcon() {
    const theme::Colors& c = theme::colors();
    const bool busy = kind_ == Kind::Busy;
    spinner_->setVisible(busy);
    icon_->setVisible(!busy);
    icons::Glyph glyph = icons::Glyph::Info;
    QColor color = c.accent;
    switch (kind_) {
        case Kind::Info: break;
        case Kind::Busy: break;
        case Kind::Success:
            glyph = icons::Glyph::Check;
            color = c.success;
            break;
        case Kind::Copied:
            glyph = icons::Glyph::Copy;
            break;
        case Kind::Warning:
            glyph = icons::Glyph::Warning;
            color = c.warning;
            break;
        case Kind::Error:
            glyph = icons::Glyph::Warning;
            color = c.danger;
            break;
    }
    icon_->setPixmap(icons::pixmap(glyph, color, 20, devicePixelRatioF()));
}

void Toast::showText(const QString& text, Kind kind) {
    kind_ = kind;
    text_->setText(text);
    // One line up to kMaxTextWidth, wrapped beyond (QLabel's own guess wraps short texts too).
    const int advance = text_->fontMetrics().horizontalAdvance(text) + 2;
    const bool wrap = advance > kMaxTextWidth;
    text_->setWordWrap(wrap);
    const int width = wrap ? kMaxTextWidth : advance;
    text_->setFixedSize(width, wrap ? text_->heightForWidth(width) : text_->sizeHint().height());
    refreshIcon();
    updateCompositing();
    if (QLayout* l = layout()) l->activate();
    placeNear(QCursor::pos());
    showFaded();
    raise();
}

void Toast::dismiss() { hideFaded(); }

QString Toast::text() const { return text_->text(); }

}  // namespace revisa::tray
