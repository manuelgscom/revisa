#include "tray/popup.hpp"

#include <QCloseEvent>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "core/catalog/catalog.hpp"
#include "tray/diff_html.hpp"
#include "tray/icons.hpp"
#include "tray/theme.hpp"

namespace revisa::tray {

namespace {

constexpr int kCardWidth = 560;
constexpr int kMessageMinWidth = 320;
constexpr int kSide = 16;        // horizontal padding of the card
constexpr int kRowIndent = 44;   // check box (14 + 18) + gap (12)
constexpr int kCheckX = 14;
constexpr int kCheckSize = 18;
constexpr int kRowTop = 8;

DiffColors diffColors(const theme::Colors& c) {
    DiffColors d;
    d.delFg = theme::css(c.delFg);
    d.delBg = theme::css(c.delBg);
    d.insFg = theme::css(c.insFg);
    d.insBg = theme::css(c.insBg);
    return d;
}

void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

QLabel* secondaryLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("role", QStringLiteral("secondary"));
    l->setFont(theme::smallFont());
    return l;
}

QLabel* keycap(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("keycap"));
    l->setFont(theme::smallFont());
    l->setAlignment(Qt::AlignCenter);
    l->setFixedHeight(l->fontMetrics().height() + 6);
    l->setMinimumWidth(l->fontMetrics().height() + 8);
    return l;
}

// One edit: the whole row is the check box. It paints its own box, hover and focus ring; the
// labels inside let the mouse through, so a click anywhere toggles it.
//
//   [✓]  despues  →  después                          (Falta tilde)
//        «después» lleva tilde: es aguda y termina en «-s»…
class EditRow final : public QCheckBox {
public:
    EditRow(const check::Edit& edit, QWidget* parent) : QCheckBox(editLabel(edit), parent), edit_(edit) {
        const std::string_view id = catalog::type_id(edit.type);
        setToolTip(QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
        setFocusPolicy(Qt::TabFocus);  // keyboard only: a click does not leave a focus ring
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setChecked(true);

        auto* row = new QVBoxLayout(this);
        row->setContentsMargins(kRowIndent, kRowTop, 12, 10);
        row->setSpacing(2);
        auto* top = new QHBoxLayout();
        top->setSpacing(12);
        title_ = new QLabel(this);
        title_->setTextFormat(Qt::RichText);
        title_->setWordWrap(true);
        title_->setFont(theme::baseFont());
        chip_ = new QLabel(typeLabel(edit.type), this);
        chip_->setObjectName(QStringLiteral("chip"));
        chip_->setFont(theme::smallFont());
        top->addWidget(title_, 1);
        top->addWidget(chip_, 0, Qt::AlignTop | Qt::AlignRight);
        row->addLayout(top);
        if (edit.explanation && !edit.explanation->empty()) {
            why_ = new QLabel(QString::fromStdString(*edit.explanation), this);
            why_->setTextFormat(Qt::PlainText);
            why_->setWordWrap(true);
            why_->setFont(theme::smallFont());
            row->addWidget(why_);
        }
        for (QLabel* l : {title_, chip_, why_}) {
            if (!l) continue;
            l->setAttribute(Qt::WA_TransparentForMouseEvents);
            l->show();  // not hidden while the popup is: the height computation counts it
        }
        connect(this, &QCheckBox::toggled, this, [this] { refresh(); });
        refresh();
    }

    // Colors for the check state and the theme in use.
    void refresh() {
        const theme::Colors& c = theme::colors();
        const bool on = isChecked();
        title_->setText(editTitleHtml(edit_, theme::css(on ? c.textSecondary : c.textTertiary),
                                      theme::css(on ? c.text : c.textTertiary)));
        chip_->setProperty("off", !on);
        repolish(chip_);
        if (why_) why_->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(on ? c.textSecondary : c.textTertiary)));
        update();
    }

    QSize sizeHint() const override { return layout()->sizeHint(); }
    QSize minimumSizeHint() const override { return layout()->minimumSize(); }

protected:
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }

    bool event(QEvent* e) override {
        switch (e->type()) {
            case QEvent::Enter:
            case QEvent::Leave:
            case QEvent::HoverEnter:
            case QEvent::HoverLeave:
                update();
                break;
            default:
                break;
        }
        return QCheckBox::event(e);
    }

    void paintEvent(QPaintEvent* /*event*/) override {
        const theme::Colors& c = theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect());
        if (underMouse() || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(isDown() ? c.pressed : c.hover);
            p.drawRoundedRect(r, 8, 8);
        }
        if (hasFocus()) {
            p.setPen(QPen(c.accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 7, 7);
        }
        // The box, centered on the first line of the title.
        const int line = title_->fontMetrics().height();
        const qreal y = kRowTop + (line - kCheckSize) / 2.0;
        theme::paintCheckBox(p, QRectF(kCheckX, y, kCheckSize, kCheckSize), isChecked(), false, isEnabled());
    }

private:
    check::Edit edit_;
    QLabel* title_ = nullptr;
    QLabel* chip_ = nullptr;
    QLabel* why_ = nullptr;
};

}  // namespace

Popup::Popup(QWidget* parent) : CardWindow(Qt::Tool | Qt::WindowStaysOnTopHint, parent) {
    setObjectName(QStringLiteral("revisa-popup"));
    setWindowTitle(QStringLiteral("revisa"));
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QVBoxLayout(card());
    layout->setContentsMargins(1, 1, 1, 1);  // inside the 1 px border
    layout->setSpacing(0);

    // Header: icon, name, status (count or message, with a spinner) and the close button.
    auto* header = new QHBoxLayout();
    header->setContentsMargins(kSide - 1, 11, 11, 11);
    header->setSpacing(8);
    icon_ = new QLabel(card());
    icon_->setFixedSize(20, 20);
    auto* name = new QLabel(QStringLiteral("revisa"), card());
    name->setProperty("role", QStringLiteral("title"));
    name->setFont(theme::strongFont());
    spinner_ = new Spinner(card(), 14);
    status_ = new QLabel(card());
    status_->setProperty("role", QStringLiteral("secondary"));
    status_->setFont(theme::baseFont());
    close_ = new QToolButton(card());
    close_->setObjectName(QStringLiteral("close"));
    close_->setFixedSize(28, 28);
    close_->setIconSize(QSize(16, 16));
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setCursor(Qt::PointingHandCursor);
    close_->setToolTip(QStringLiteral("Cancelar (Esc)"));
    connect(close_, &QToolButton::clicked, this, &Popup::cancel);
    header->addWidget(icon_);
    header->addWidget(name);
    header->addSpacing(2);
    header->addWidget(spinner_);
    header->addWidget(status_);
    header->addStretch(1);
    header->addWidget(close_);
    layout->addLayout(header);

    // Body: the diff in a soft panel, then the rows; scrolls when there are many edits.
    body_ = new QWidget(card());
    auto* bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    auto* content = new QWidget();
    content->setObjectName(QStringLiteral("scrollContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(kSide - 1, 1, kSide - 1, 8);
    contentLayout->setSpacing(10);
    auto* panel = new QFrame(content);
    panel->setObjectName(QStringLiteral("diffPanel"));
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(kCheckX, 10, kCheckX, 10);
    diff_ = new QLabel(panel);
    diff_->setTextFormat(Qt::RichText);
    diff_->setWordWrap(true);
    diff_->setFont(theme::baseFont());
    panelLayout->addWidget(diff_);
    contentLayout->addWidget(panel);
    editsBox_ = new QWidget(content);
    editsLayout_ = new QVBoxLayout(editsBox_);
    editsLayout_->setContentsMargins(0, 0, 0, 0);
    editsLayout_->setSpacing(2);
    contentLayout->addWidget(editsBox_);
    contentLayout->addStretch(1);
    for (QWidget* w : {static_cast<QWidget*>(panel), static_cast<QWidget*>(diff_), editsBox_}) w->show();

    scroll_ = new QScrollArea(body_);
    scroll_->setWidget(content);
    content->show();
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFocusPolicy(Qt::NoFocus);
    scroll_->viewport()->setAutoFillBackground(false);
    bodyLayout->addWidget(scroll_);
    layout->addWidget(body_);

    // Footer: keyboard hints, Cancelar and Aplicar.
    footer_ = new QWidget(card());
    auto* footerLayout = new QVBoxLayout(footer_);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);
    auto* divider = new QFrame(footer_);
    divider->setObjectName(QStringLiteral("divider"));
    footerLayout->addWidget(divider);
    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(kSide - 1, 12, kSide - 1, 13);
    actions->setSpacing(6);
    actions->addWidget(keycap(QStringLiteral("↵"), footer_), 0, Qt::AlignVCenter);
    actions->addWidget(secondaryLabel(QStringLiteral("Aplicar"), footer_), 0, Qt::AlignVCenter);
    actions->addSpacing(12);
    actions->addWidget(keycap(QStringLiteral("Esc"), footer_), 0, Qt::AlignVCenter);
    actions->addWidget(secondaryLabel(QStringLiteral("Cancelar"), footer_), 0, Qt::AlignVCenter);
    actions->addStretch(1);
    cancelButton_ = new QPushButton(QStringLiteral("Cancelar"), footer_);
    cancelButton_->setProperty("variant", QStringLiteral("ghost"));
    applyButton_ = new QPushButton(QStringLiteral("Aplicar"), footer_);
    applyButton_->setProperty("variant", QStringLiteral("primary"));
    applyButton_->setDefault(true);
    applyButton_->setIconSize(QSize(16, 16));
    for (QPushButton* b : {cancelButton_, applyButton_}) {
        b->setAutoDefault(false);
        b->setFocusPolicy(Qt::NoFocus);  // Enter and Esc belong to the popup
        b->setCursor(Qt::PointingHandCursor);
        b->setFont(theme::strongFont());
    }
    connect(cancelButton_, &QPushButton::clicked, this, &Popup::cancel);
    connect(applyButton_, &QPushButton::clicked, this, &Popup::apply);
    actions->addSpacing(2);
    actions->addWidget(cancelButton_);
    actions->addWidget(applyButton_);
    footerLayout->addLayout(actions);
    layout->addWidget(footer_);

    applyTheme();
}

void Popup::applyTheme() {
    CardWindow::applyTheme();
    const theme::Colors& c = theme::colors();
    const qreal dpr = devicePixelRatioF();
    icon_->setPixmap(icons::appIcon(20, dpr));
    close_->setIcon(icons::icon(icons::Glyph::Close, c.textSecondary, 16));
    applyButton_->setIcon(icons::icon(icons::Glyph::Check, c.onAccent, 16));
    for (QCheckBox* b : boxes_) static_cast<EditRow*>(b)->refresh();
    if (hasResult_) refreshDiff();
}

void Popup::showMessage(const QString& message) {
    clearEdits();
    hasResult_ = false;
    original_.clear();
    edits_.clear();
    status_->setText(message);
    spinner_->show();
    body_->hide();
    footer_->hide();
    card()->setMinimumWidth(kMessageMinWidth);
    card()->setMaximumWidth(QWIDGETSIZE_MAX);
    placeAndShow();
}

void Popup::showResult(const QString& original, const std::vector<check::Edit>& edits) {
    clearEdits();
    original_ = original;
    edits_ = edits;
    hasResult_ = true;
    spinner_->hide();
    status_->setText(correctionsCount(edits_.size()));
    for (const check::Edit& e : edits_) {
        auto* row = new EditRow(e, editsBox_);
        row->show();  // see the height computation below
        connect(row, &QCheckBox::toggled, this, [this] { refreshDiff(); });
        editsLayout_->addWidget(row);
        boxes_.push_back(row);
    }
    refreshDiff();
    body_->show();
    footer_->show();
    card()->setFixedWidth(kCardWidth);

    // The scroll area as tall as its content, up to 60 % of the screen. Layouts skip hidden
    // widgets, and a child of a window that has not been shown yet counts as hidden unless
    // show() was called on it: hence the show() calls on everything inside the scroll area.
    ensurePolished();  // the stylesheet's paddings count (chips)
    fitScrollHeight();
    placeAndShow();
    // Real fonts can wrap an explanation onto one more line than predicted before the window
    // existed: once shown and laid out, fit again with the viewport's actual width.
    QTimer::singleShot(0, this, [this] {
        if (!isVisible()) return;
        const int before = scroll_->height();
        fitScrollHeight();
        if (scroll_->height() != before) {
            if (QLayout* l = layout()) l->activate();
            adjustSize();
            placeNear(QCursor::pos());
        }
    });
}

// The scroll area as tall as its content (for the viewport's real width), up to 60 % of the
// screen; only taller content scrolls.
void Popup::fitScrollHeight() {
    QWidget* content = scroll_->widget();
    content->layout()->invalidate();  // it caches whether it has height-for-width items
    if (QLayout* l = card()->layout()) l->activate();  // the card has its fixed width: sizes inside
    if (QLayout* l = layout()) l->activate();
    // The area's own width (no scrollbar is counted): measuring the viewport while a bar is up
    // makes the text wrap more and the content look taller than it is.
    int width = scroll_->contentsRect().width();
    if (width < 100) width = kCardWidth - 2;  // not laid out yet: the card minus its border
    const int wanted = content->hasHeightForWidth() ? content->heightForWidth(width) : content->sizeHint().height();
    const QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const int maxHeight = std::max(40, screen ? screen->availableGeometry().height() * 6 / 10 : 600);
    // A scrollbar only when the content really does not fit; otherwise none at all, so it can
    // never appear for a pixel of rounding and steal width from the text.
    const bool fits = wanted <= maxHeight;
    scroll_->setVerticalScrollBarPolicy(fits ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    scroll_->setFixedHeight(fits ? std::max(40, wanted) : maxHeight);
}

std::vector<bool> Popup::enabled() const {
    std::vector<bool> out;
    out.reserve(boxes_.size());
    for (const QCheckBox* b : boxes_) out.push_back(b->isChecked());
    return out;
}

QString Popup::selectedText() const { return applyEnabled(original_, edits_, enabled()); }

QString Popup::diffText() const { return diff_->text(); }

void Popup::apply() {
    if (!hasResult_) return;
    emit accepted(selectedText());
}

void Popup::cancel() { emit cancelled(); }

void Popup::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            apply();
            return;
        case Qt::Key_Escape:
            cancel();
            return;
        default:
            CardWindow::keyPressEvent(event);
    }
}

void Popup::closeEvent(QCloseEvent* event) {
    if (!isVisible()) {
        CardWindow::closeEvent(event);
        return;
    }
    event->ignore();
    cancel();
}

void Popup::clearEdits() {
    for (QCheckBox* b : boxes_) b->disconnect(this);
    boxes_.clear();
    while (QLayoutItem* item = editsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    diff_->clear();
}

void Popup::refreshDiff() { diff_->setText(diffHtml(original_, edits_, enabled(), diffColors(theme::colors()))); }

void Popup::placeAndShow() {
    updateCompositing();
    if (QLayout* l = layout()) l->activate();
    placeNear(QCursor::pos());
    showFaded();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
}

}  // namespace revisa::tray
