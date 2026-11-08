// Clipboard-only platform: Wayland (no global hotkeys, no selection, no simulated keys) or
// REVISA_TRAY_PLATFORM=clipboard. Portable Qt only: it is also built on Windows.

#include "tray/platform/platform.hpp"

#include <QClipboard>
#include <QGuiApplication>

namespace revisa::tray {

namespace {

class ClipboardOnlyPlatform final : public Platform {
public:
    QString name() const override { return QStringLiteral("clipboard"); }

    bool registerHotkey(const QKeySequence& /*keys*/, QString* error) override {
        if (error) {
            *error = QStringLiteral(
                "En Wayland no hay atajos globales: copia el texto y usa «Corregir el "
                "portapapeles» en la bandeja o un atajo del escritorio que ejecute "
                "revisa-tray --check-clipboard.");
        }
        return false;
    }

    void unregisterHotkey() override {}

    WindowRef activeWindow() override { return 0; }

    std::optional<QString> readSelection(WindowRef /*window*/) override {
        const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
        if (text.trimmed().isEmpty()) return std::nullopt;
        return text;
    }

    bool activate(WindowRef /*window*/) override { return false; }

    bool paste(WindowRef /*window*/, const QString& text) override {
        QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
        return false;
    }

    bool canSimulateInput() const override { return false; }

    // Wayland compositors always composite (and so does DWM, if this mode is forced on Windows).
    // Forced on X11 (REVISA_TRAY_PLATFORM=clipboard), the X server may have no compositor and this
    // portable code cannot ask it: the safe, opaque look.
    bool hasCompositor() const override { return QGuiApplication::platformName() != QLatin1String("xcb"); }
};

}  // namespace

std::unique_ptr<Platform> makeClipboardOnlyPlatform() {
    return std::make_unique<ClipboardOnlyPlatform>();
}

}  // namespace revisa::tray
