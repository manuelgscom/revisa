// Operating-system layer of revisa-tray. Everything that touches X11, Wayland or Win32 lives
// behind this interface, in src/tray/platform/. The rest of src/tray/ only uses Qt, the standard
// library and revisa_core, so a new OS only needs a new platform/<os>.cpp.
//
// Flow of the hotkey, as the app drives it:
//
//   hotkeyPressed() ─► w = activeWindow() ─► text = readSelection(w) ─► revisa_core ─► popup
//                                                                                    │ Enter
//                          paste(w, corrected) ◄─────────────────────────────────────┘
//
// In clipboard-only mode (Wayland, canSimulateInput() == false) there is no hotkey: the user
// copies the text, triggers the check from the tray menu or with `revisa-tray --check-clipboard`,
// the app shows the popup first and only then calls readSelection() (Wayland only hands the
// clipboard to a focused window), and paste() leaves the text in the clipboard.
//
// All methods run on the GUI thread. Blocking waits (modifier release, clipboard change) are
// short (< 1 s) and use a local QEventLoop, so the GUI keeps processing events.

#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace revisa::tray {

// Opaque handle of a top-level window of another app (X11 Window, Win32 HWND). 0 = none.
using WindowRef = quint64;

class Platform : public QObject {
    Q_OBJECT

public:
    ~Platform() override = default;

    // Short name for logs and the About box: "x11", "clipboard", "win32".
    virtual QString name() const = 0;

    // Grabs `keys` (one chord, e.g. Ctrl+Alt+Space) system-wide, replacing any previous grab.
    // Returns false and fills `error` (Spanish, for the user) if the OS refuses it, typically
    // because another program already owns it. Without global hotkeys (clipboard-only mode) it
    // returns false with an explanatory error and the app only offers the tray menu.
    virtual bool registerHotkey(const QKeySequence& keys, QString* error) = 0;
    virtual void unregisterHotkey() = 0;

    // The window that has the focus right now (the one the user selected text in). Called as
    // soon as the hotkey fires, before any window of ours shows up. 0 if unknown.
    virtual WindowRef activeWindow() = 0;

    // The text the user selected in `window`:
    //   X11:   the PRIMARY selection; if empty, the Ctrl+C fallback (save CLIPBOARD, simulate
    //          Ctrl+C after the hotkey modifiers are released, wait <= 300 ms for the change,
    //          read it, restore CLIPBOARD).
    //   win32: save the clipboard, SendInput Ctrl+C, wait for GetClipboardSequenceNumber.
    //   clipboard-only: the CLIPBOARD contents.
    // nullopt if there is nothing to read (empty selection, timeout).
    virtual std::optional<QString> readSelection(WindowRef window) = 0;

    // Gives the focus back to `window`. False if the OS did not let us.
    virtual bool activate(WindowRef window) = 0;

    // Replaces the selection in `window` with `text`: put `text` in the clipboard, activate
    // `window`, simulate the paste shortcut (Ctrl+V, or Ctrl+Shift+V in terminals) and, ~500 ms
    // later, restore the previous clipboard if it still holds `text`.
    // Returns false if the text could not be pasted; the text is then left in the clipboard so
    // the app can tell the user «Copiado: pega con Ctrl+V». In clipboard-only mode it always
    // returns false after leaving the text in the clipboard.
    virtual bool paste(WindowRef window, const QString& text) = 0;

    // False in clipboard-only mode: no hotkey, no reading the selection, no simulated keys.
    virtual bool canSimulateInput() const = 0;

    // True if a compositing manager draws the windows, so a translucent top-level window shows
    // what is behind it (the popup and the toast then float: rounded corners, shadow, fade).
    // Without one, translucent pixels come out black and they use an opaque, square card.
    //   X11:   someone owns the _NET_WM_CM_S<screen> selection (asked on every call: it can change).
    //   clipboard-only (Wayland): true, Wayland compositors always composite; false if it was
    //          forced on an X11 session (Qt platform xcb), which this portable code cannot ask.
    //   win32: always true, DWM composites since Windows 8.
    virtual bool hasCompositor() const = 0;

signals:
    // The global hotkey was pressed. Emitted on the GUI thread.
    void hotkeyPressed();
};

// The platform for this session (defined in platform/x11.cpp on Linux, platform/win32.cpp on
// Windows): X11, or clipboard-only on Wayland (XDG_SESSION_TYPE=wayland or
// WAYLAND_DISPLAY set) or when REVISA_TRAY_PLATFORM=clipboard. On Windows, win32. Must be called
// after the QApplication exists.
std::unique_ptr<Platform> makePlatform();

// Clipboard-only platform, available on every OS (platform/clipboard_only.cpp).
std::unique_ptr<Platform> makeClipboardOnlyPlatform();

// Start with the session, independent of X11/Wayland: ~/.config/autostart/revisa.desktop on
// Linux (platform/autostart_linux.cpp; Exec = $APPIMAGE if set, else this binary), the HKCU
// Run key on Windows (platform/autostart_win32.cpp). setAutostart returns false and fills
// `error` (Spanish) if it could not write it.
bool autostartEnabled();
bool setAutostart(bool enabled, QString* error);

}  // namespace revisa::tray
