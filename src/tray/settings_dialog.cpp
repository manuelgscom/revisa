#include "tray/settings_dialog.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include <cstddef>
#include <utility>

#include "core/catalog/catalog.hpp"
#include "core/infer/cpu.hpp"
#include "tray/diff_html.hpp"
#include "tray/icons.hpp"
#include "tray/theme.hpp"

namespace revisa::tray {

namespace {

constexpr int kDialogWidth = 540;
constexpr int kMargin = 24;
constexpr int kSectionPad = 16;

QLabel* helper(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("role", QStringLiteral("secondary"));
    l->setFont(theme::smallFont());
    l->setWordWrap(true);
    return l;
}

// A setting's name with its explanation below.
QVBoxLayout* textBlock(const QString& title, const QString& help, QWidget* parent) {
    auto* block = new QVBoxLayout();
    block->setSpacing(2);
    auto* name = new QLabel(title, parent);
    name->setFont(theme::baseFont());
    block->addWidget(name);
    block->addWidget(helper(help, parent));
    return block;
}

// A titled group: the title above, the content in a rounded card.
//
//   Atajo
//   ╭──────────────────────────────╮
//   │ …                            │
//   ╰──────────────────────────────╯
QVBoxLayout* section(QVBoxLayout* page, const QString& title, QWidget* parent) {
    auto* name = new QLabel(title, parent);
    name->setObjectName(QStringLiteral("sectionTitle"));
    name->setFont(theme::strongFont());
    name->setContentsMargins(2, 0, 0, 0);
    page->addWidget(name);
    page->addSpacing(6);
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("section"));
    auto* inner = new QVBoxLayout(card);
    inner->setContentsMargins(kSectionPad, 14, kSectionPad, 14);
    inner->setSpacing(10);
    page->addWidget(card);
    page->addSpacing(18);
    return inner;
}

}  // namespace

SettingsDialog::SettingsDialog(const Settings& current, ApplyFn applyFn, QWidget* parent)
    : QDialog(parent), initial_(current), apply_(std::move(applyFn)) {
    setWindowTitle(QStringLiteral("Ajustes de revisa"));
    setObjectName(QStringLiteral("revisa-settings"));
    setFixedWidth(kDialogWidth);

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(kMargin, 20, kMargin, 20);
    page->setSpacing(0);

    // Header: icon, title, subtitle.
    auto* head = new QHBoxLayout();
    head->setSpacing(12);
    appIcon_ = new QLabel(this);
    appIcon_->setFixedSize(36, 36);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Ajustes"), this);
    QFont big = theme::strongFont();
    if (big.pointSizeF() > 0) big.setPointSizeF(big.pointSizeF() + 3);
    title->setFont(big);
    titles->addWidget(title);
    titles->addWidget(helper(QStringLiteral("revisa corrige el texto que seleccionas, sin conexión."), this));
    head->addWidget(appIcon_, 0, Qt::AlignVCenter);
    head->addLayout(titles, 1);
    page->addLayout(head);
    page->addSpacing(20);

    // Atajo
    QVBoxLayout* keys = section(page, QStringLiteral("Atajo"), this);
    keys->addLayout(textBlock(QStringLiteral("Atajo de teclado"),
                              QStringLiteral("Selecciona texto en cualquier aplicación y pulsa el atajo para corregirlo."),
                              this));
    hotkey_ = new QKeySequenceEdit(
        QKeySequence::fromString(QString::fromStdString(current.config.hotkey), QKeySequence::PortableText), this);
    hotkey_->setMaximumSequenceLength(1);
    hotkey_->setClearButtonEnabled(true);
    auto* reset = new QPushButton(QStringLiteral("Restablecer"), this);
    reset->setAutoDefault(false);
    reset->setProperty("variant", QStringLiteral("ghost"));
    reset->setToolTip(QStringLiteral("Volver a %1").arg(QString::fromLatin1(config::kDefaultHotkey)));
    connect(reset, &QPushButton::clicked, this, [this] {
        hotkey_->setKeySequence(QKeySequence::fromString(QString::fromLatin1(config::kDefaultHotkey),
                                                         QKeySequence::PortableText));
    });
    auto* hotkeyRow = new QHBoxLayout();
    hotkeyRow->setSpacing(8);
    keyIcon_ = new QLabel(this);
    keyIcon_->setFixedSize(20, 20);
    hotkeyRow->addWidget(keyIcon_);
    hotkeyRow->addSpacing(2);
    hotkeyRow->addWidget(hotkey_, 1);
    hotkeyRow->addWidget(reset);
    keys->addLayout(hotkeyRow);

    // Correcciones
    QVBoxLayout* fixes = section(page, QStringLiteral("Correcciones"), this);
    fixes->addWidget(helper(QStringLiteral("Desmarca los tipos de error que no quieras que revisa corrija."), this));
    types_ = new QListWidget(this);
    types_->setObjectName(QStringLiteral("types"));
    types_->setFlow(QListView::LeftToRight);
    types_->setWrapping(true);
    types_->setResizeMode(QListView::Adjust);
    types_->setUniformItemSizes(true);
    types_->setSelectionMode(QAbstractItemView::NoSelection);
    types_->setFocusPolicy(Qt::StrongFocus);
    types_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int listWidth = kDialogWidth - 2 * kMargin - 2 * kSectionPad - 2;
    const int column = (listWidth - 24) / 2;  // room for the scroll bar
    types_->setGridSize(QSize(column, 32));
    for (std::size_t i = 0; i < catalog::kTypeCount; ++i) {
        const auto type = static_cast<catalog::Type>(i);
        if (type == catalog::Type::Otro) continue;  // guard 5 already drops edits outside the catalog
        const std::string_view id = catalog::type_id(type);
        auto* item = new QListWidgetItem(typeLabel(type), types_);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(current.config.disabled_types.count(type) ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setToolTip(QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
        item->setSizeHint(QSize(column - 4, 30));
    }
    // A click anywhere on the row toggles it, not only on the box.
    connect(types_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (clickOnBox_) {
            clickOnBox_ = false;
            return;
        }
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });
    connect(types_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { clickOnBox_ = true; });
    connect(types_, &QListWidget::itemPressed, this, [this](QListWidgetItem*) { clickOnBox_ = false; });
    types_->setFixedHeight(32 * 7 + 4);
    fixes->addWidget(types_);

    // Rendimiento
    QVBoxLayout* perf = section(page, QStringLiteral("Rendimiento"), this);
    auto* threadsRow = new QHBoxLayout();
    threadsRow->setSpacing(6);
    const int cores = infer::physical_cores();
    threadsRow->addLayout(textBlock(QStringLiteral("Hilos del procesador"),
                                    cores > 0 ? QStringLiteral("«Automático» usa los %1 núcleos físicos.").arg(cores)
                                              : QStringLiteral("«Automático» usa los núcleos físicos."),
                                    this),
                          1);
    threads_ = new QSpinBox(this);
    // Up to the physical cores, or more if the config already asks for more: never rewrite it.
    threads_->setRange(0, std::min(config::kMaxThreads, std::max({1, cores, current.config.threads})));
    threads_->setSpecialValueText(QStringLiteral("Automático"));
    threads_->setValue(current.config.threads);
    threads_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    threads_->setAlignment(Qt::AlignCenter);
    threads_->setFixedWidth(112);
    minus_ = new QToolButton(this);
    plus_ = new QToolButton(this);
    for (QToolButton* b : {minus_, plus_}) {
        b->setProperty("variant", QStringLiteral("stepper"));
        b->setFixedSize(32, 32);
        b->setIconSize(QSize(16, 16));
        b->setAutoRepeat(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    minus_->setToolTip(QStringLiteral("Menos hilos"));
    plus_->setToolTip(QStringLiteral("Más hilos"));
    connect(minus_, &QToolButton::clicked, threads_, &QSpinBox::stepDown);
    connect(plus_, &QToolButton::clicked, threads_, &QSpinBox::stepUp);
    connect(threads_, &QSpinBox::valueChanged, this, [this] { updateStepper(); });
    threadsRow->addWidget(minus_, 0, Qt::AlignVCenter);
    threadsRow->addWidget(threads_, 0, Qt::AlignVCenter);
    threadsRow->addWidget(plus_, 0, Qt::AlignVCenter);
    perf->addLayout(threadsRow);

    // Sistema
    QVBoxLayout* sys = section(page, QStringLiteral("Sistema"), this);
    autostart_ = new QCheckBox(QStringLiteral("Arrancar con el sistema"), this);
    autostart_->setChecked(current.autostart);
    autostart_->setCursor(Qt::PointingHandCursor);
    sys->setSpacing(2);
    sys->addWidget(autostart_);
    auto* autostartHelp = helper(QStringLiteral("revisa se queda en la bandeja al iniciar la sesión."), this);
    autostartHelp->setContentsMargins(28, 0, 0, 0);
    sys->addWidget(autostartHelp);

    // Buttons
    page->addSpacing(2);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"), this);
    ok_ = new QPushButton(QStringLiteral("Aceptar"), this);
    ok_->setProperty("variant", QStringLiteral("primary"));
    ok_->setDefault(true);
    ok_->setIconSize(QSize(16, 16));
    for (QPushButton* b : {cancel, ok_}) {
        b->setMinimumWidth(104);
        b->setFont(theme::strongFont());
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(ok_, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &SettingsDialog::reject);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(ok_);
    page->addLayout(buttons);

    connect(theme::notifier(), &theme::Notifier::changed, this, [this] { applyTheme(); });
    applyTheme();
    updateStepper();
    ok_->setFocus();  // not the shortcut editor: a stray key would replace the shortcut
}

void SettingsDialog::applyTheme() {
    const theme::Colors& c = theme::colors();
    setStyleSheet(theme::styleSheet(c));
    const qreal dpr = devicePixelRatioF();
    appIcon_->setPixmap(icons::appIcon(36, dpr));
    keyIcon_->setPixmap(icons::pixmap(icons::Glyph::Keyboard, c.textSecondary, 20, dpr));
    minus_->setIcon(icons::icon(icons::Glyph::Minus, c.text, 16));
    plus_->setIcon(icons::icon(icons::Glyph::Plus, c.text, 16));
    ok_->setIcon(icons::icon(icons::Glyph::Check, c.onAccent, 16));
}

void SettingsDialog::updateStepper() {
    minus_->setEnabled(threads_->value() > threads_->minimum());
    plus_->setEnabled(threads_->value() < threads_->maximum());
}

Settings SettingsDialog::values() const {
    Settings s = initial_;
    s.config.hotkey = hotkey_->keySequence().toString(QKeySequence::PortableText).toStdString();
    s.config.threads = threads_->value();
    for (int row = 0; row < types_->count(); ++row) {
        const QListWidgetItem* item = types_->item(row);
        const auto type = static_cast<catalog::Type>(item->data(Qt::UserRole).toInt());
        if (item->checkState() == Qt::Checked) s.config.disabled_types.erase(type);
        else s.config.disabled_types.insert(type);
    }
    s.autostart = autostart_->isChecked();
    return s;
}

void SettingsDialog::accept() {
    const Settings s = values();
    QString error;
    if (s.config.hotkey.empty()) error = QStringLiteral("Elige un atajo.");
    else if (apply_ && !apply_(s, &error)) {
        if (error.isEmpty()) error = QStringLiteral("No se pudieron aplicar los ajustes.");
    } else {
        QDialog::accept();
        return;
    }
    QMessageBox::warning(this, QStringLiteral("revisa"), error);
}

}  // namespace revisa::tray
