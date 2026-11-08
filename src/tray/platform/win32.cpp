// Win32 platform: RegisterHotKey + WM_HOTKEY, a simulated
// Ctrl+C (SendInput) and GetClipboardSequenceNumber to read, SetForegroundWindow + a simulated
// Ctrl+V to paste. Same structure and timings as x11.cpp. makePlatform() picks this or the
// clipboard-only one.
//
// Known limit (UIPI): a normal process cannot send keys to a window that runs as administrator.
// SendInput reports no error; the copy then never changes the clipboard (readSelection logs it
// and returns nullopt) and the paste does nothing.

#include "tray/platform/keys_win32.hpp"
#include "tray/platform/platform.hpp"

#include <QAbstractNativeEventFilter>
#include <QClipboard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>

#include <initializer_list>
#include <memory>
#include <vector>

#include <windows.h>

namespace revisa::tray {

namespace {

constexpr int kHotkeyId = 0x5256;     // application ids are 0x0000-0xBFFF; one hotkey per process
constexpr int kKeysWaitMs = 500;      // modifiers of the hotkey still held
constexpr int kCopyWaitMs = 300;      // Ctrl+C: wait for the clipboard to change
constexpr int kActivateWaitMs = 300;  // SetForegroundWindow taking effect
constexpr int kSettleMs = 80;         // after activating, before Ctrl+V
constexpr int kRestoreMs = 1500;      // after Ctrl+V, restore the previous clipboard (slow targets)
constexpr WORD kVkC = 0x43;           // 'C'
constexpr WORD kVkV = 0x56;           // 'V'
constexpr WORD kVkUnassigned = 0xE8;  // "unassigned" in the VK table: a key no app reacts to

// Runs the Qt event loop for `ms` (no user input), so clipboard and COM requests keep being served.
void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

// Only the text formats, as on X11: a slow owner rendering images or files would block us, and
// they are not what the user copies before correcting a text.
std::unique_ptr<QMimeData> cloneMime(const QMimeData* src) {
    auto out = std::make_unique<QMimeData>();
    if (src) {
        for (const QString& format : src->formats()) {
            if (format.startsWith(QLatin1String("text/plain")) || format == QLatin1String("text/html"))
                out->setData(format, src->data(format));
        }
    }
    return out;
}

void restoreClipboard(const QMimeData& saved) {
    QClipboard* cb = QGuiApplication::clipboard();
    if (saved.formats().isEmpty()) cb->clear(QClipboard::Clipboard);
    else cb->setMimeData(cloneMime(&saved).release(), QClipboard::Clipboard);
}

HWND toHwnd(WindowRef w) { return reinterpret_cast<HWND>(static_cast<quintptr>(w)); }
WindowRef fromHwnd(HWND h) { return static_cast<WindowRef>(reinterpret_cast<quintptr>(h)); }

bool isDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

INPUT keyInput(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    return in;
}

bool send(std::vector<INPUT>& inputs) {
    const UINT n = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), static_cast<int>(sizeof(INPUT)));
    if (n != inputs.size()) qInfo("win32: SendInput sent %u of %zu events (error %lu)", n, inputs.size(), GetLastError());
    return n == inputs.size();
}

// Appends key-ups for the modifiers still physically held (they would add up with ours: Ctrl+Alt+V),
// preceded by an unassigned key, so a lone Alt or Win release opens no menu bar or Start menu.
void releaseHeldModifiers(std::vector<INPUT>& inputs) {
    bool any = false;
    for (const int vk : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN}) {
        if (!isDown(vk)) continue;
        if (!any) {
            inputs.push_back(keyInput(kVkUnassigned, false));
            inputs.push_back(keyInput(kVkUnassigned, true));
            any = true;
        }
        inputs.push_back(keyInput(static_cast<WORD>(vk), true));
    }
}

bool sendChord(std::initializer_list<WORD> mods, WORD key) {
    std::vector<INPUT> inputs;
    releaseHeldModifiers(inputs);
    for (const WORD m : mods) inputs.push_back(keyInput(m, false));
    inputs.push_back(keyInput(key, false));
    inputs.push_back(keyInput(key, true));
    for (auto it = mods.end(); it != mods.begin();) inputs.push_back(keyInput(*--it, true));
    return send(inputs);
}

// The foreground lock lets a process take the foreground if it sent the last input event.
void tapUnassignedKey() {
    std::vector<INPUT> inputs = {keyInput(kVkUnassigned, false), keyInput(kVkUnassigned, true)};
    send(inputs);
}

bool isConsole(HWND hwnd) {
    wchar_t cls[256] = {};
    const int n = hwnd ? GetClassNameW(hwnd, cls, 256) : 0;
    return n > 0 && win32::isConsoleClass(QString::fromWCharArray(cls, n));
}

class Win32Platform final : public Platform, public QAbstractNativeEventFilter {
public:
    Win32Platform() { qApp->installNativeEventFilter(this); }

    ~Win32Platform() override {
        if (qApp) qApp->removeNativeEventFilter(this);
        unregisterHotkey();
    }

    QString name() const override { return QStringLiteral("win32"); }

    bool registerHotkey(const QKeySequence& keys, QString* error) override {
        unregisterHotkey();
        const QString shown = keys.toString(QKeySequence::NativeText);
        const win32::Chord chord = win32::chordFromKeys(
            keys, [](char16_t c) { return static_cast<int>(VkKeyScanW(static_cast<WCHAR>(c))); });
        if (chord.vk == 0) {
            if (error) {
                *error = QStringLiteral("El atajo %1 no se puede usar en Windows: combina Ctrl, Alt, Mayús o Win "
                                        "con otra tecla. Elige otro en Ajustes.")
                             .arg(shown);
            }
            return false;
        }
        // Thread hotkey (no window): WM_HOTKEY lands in the GUI thread's queue, seen by the filter.
        if (!RegisterHotKey(nullptr, kHotkeyId, chord.mods | MOD_NOREPEAT, chord.vk)) {
            const DWORD err = GetLastError();
            qInfo("win32: RegisterHotKey refused for %s (error %lu)", qPrintable(keys.toString()), err);
            if (error) {
                *error = err == static_cast<DWORD>(ERROR_HOTKEY_ALREADY_REGISTERED)
                             ? QStringLiteral("El atajo %1 ya lo usa otro programa. Elige otro en Ajustes.").arg(shown)
                             : QStringLiteral("Windows no deja usar el atajo %1 (error %2). Elige otro en Ajustes.")
                                   .arg(shown, QString::number(err));
            }
            return false;
        }
        registered_ = true;
        hotkeyVk_ = chord.vk;
        qInfo("win32: hotkey %s registered (vk 0x%x, mods 0x%x)", qPrintable(keys.toString()), chord.vk, chord.mods);
        return true;
    }

    void unregisterHotkey() override {
        if (!registered_) return;
        UnregisterHotKey(nullptr, kHotkeyId);
        registered_ = false;
        hotkeyVk_ = 0;
    }

    WindowRef activeWindow() override { return fromHwnd(GetForegroundWindow()); }

    std::optional<QString> readSelection(WindowRef window) override {
        // The system swallows the hotkey's key, so the target sees Alt (or Win) pressed and released
        // with nothing in between: menu mode (key tips in Notepad, the menu in Edge), which then
        // ignores our Ctrl+C. An unassigned key while it is still held breaks the "lone Alt".
        if (isDown(VK_MENU) || isDown(VK_LWIN) || isDown(VK_RWIN)) tapUnassignedKey();
        QClipboard* cb = QGuiApplication::clipboard();
        const auto saved = cloneMime(cb->mimeData(QClipboard::Clipboard));
        waitKeysReleased();
        const DWORD before = GetClipboardSequenceNumber();
        // Consoles: Ctrl+Shift+C, since a bare Ctrl+C would interrupt the program running there.
        const bool sent = isConsole(toHwnd(window)) ? sendChord({VK_CONTROL, VK_SHIFT}, kVkC)
                                                    : sendChord({VK_CONTROL}, kVkC);
        QString text;
        if (sent && waitClipboardChange(before)) {
            text = cb->text(QClipboard::Clipboard);
            restoreClipboard(*saved);
        } else if (sent) {
            // Who still has the clipboard open, if anyone: copying waits for the previous owner,
            // and a hung owner blocks it.
            const HWND open = GetOpenClipboardWindow();
            DWORD openPid = 0;
            if (open) GetWindowThreadProcessId(open, &openPid);
            qInfo("win32: no selection (Ctrl+C did not change the clipboard within %d ms; clipboard open by pid %lu; "
                  "a window running as administrator ignores simulated keys)",
                  kCopyWaitMs, openPid);
        }
        if (text.trimmed().isEmpty()) return std::nullopt;
        return text;
    }

    bool activate(WindowRef window) override {
        const HWND hwnd = toHwnd(window);
        if (!hwnd || !IsWindow(hwnd)) return false;
        if (GetForegroundWindow() == hwnd) return true;
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (attempt == 1) tapUnassignedKey();  // retry as the sender of the last input event
            forceForeground(hwnd);
            if (waitActive(hwnd, attempt == 0 ? kActivateWaitMs : 100)) return true;
        }
        qInfo("win32: could not activate window 0x%llx", static_cast<unsigned long long>(window));
        return false;
    }

    bool paste(WindowRef window, const QString& text) override {
        QClipboard* cb = QGuiApplication::clipboard();
        const std::shared_ptr<QMimeData> saved = cloneMime(cb->mimeData(QClipboard::Clipboard));
        cb->setText(text, QClipboard::Clipboard);
        if (!activate(window)) return false;
        pump(kSettleMs);
        waitKeysReleased();
        const bool sent = isConsole(toHwnd(window)) ? sendChord({VK_CONTROL, VK_SHIFT}, kVkV)
                                                    : sendChord({VK_CONTROL}, kVkV);
        if (!sent) return false;
        QTimer::singleShot(kRestoreMs, this, [saved, text] {
            if (QGuiApplication::clipboard()->text(QClipboard::Clipboard) == text) restoreClipboard(*saved);
        });
        return true;
    }

    bool canSimulateInput() const override { return true; }

    bool hasCompositor() const override { return true; }  // DWM always composites (Windows 8+)

    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* /*result*/) override {
        if (!registered_ || type != "windows_generic_MSG") return false;
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message != WM_HOTKEY || msg->hwnd != nullptr || msg->wParam != static_cast<WPARAM>(kHotkeyId))
            return false;
        // MOD_NOREPEAT: holding the hotkey fires once.
        QMetaObject::invokeMethod(this, [this] { emit hotkeyPressed(); }, Qt::QueuedConnection);
        return true;
    }

private:
    // SetForegroundWindow with our thread attached to the foreground one's input, which lifts
    // the foreground lock in most cases.
    static void forceForeground(HWND hwnd) {
        const DWORD self = GetCurrentThreadId();
        const DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const bool attached = fg != 0 && fg != self && AttachThreadInput(self, fg, TRUE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        if (attached) AttachThreadInput(self, fg, FALSE);
    }

    static bool waitActive(HWND hwnd, int ms) {
        QElapsedTimer t;
        t.start();
        while (GetForegroundWindow() != hwnd) {
            if (t.elapsed() >= ms) return false;
            pump(10);
        }
        return true;
    }

    // Waits (<= kCopyWaitMs) for another app to change the clipboard and close it again, so the
    // read below does not find it still open.
    static bool waitClipboardChange(DWORD before) {
        QElapsedTimer t;
        t.start();
        for (;;) {
            const bool changed = GetClipboardSequenceNumber() != before;
            if (changed && GetOpenClipboardWindow() == nullptr) return true;
            if (t.elapsed() >= kCopyWaitMs) return changed;
            pump(10);
        }
    }

    // Simulated keys must not mix with the held hotkey: wait until modifiers and hotkey key are up.
    bool waitKeysReleased() {
        QElapsedTimer t;
        t.start();
        for (;;) {
            bool down = hotkeyVk_ != 0 && isDown(static_cast<int>(hotkeyVk_));
            for (const int vk : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN}) down = down || isDown(vk);
            if (!down) return true;
            if (t.elapsed() >= kKeysWaitMs) {
                qWarning("win32: keys still held after %d ms; simulating anyway", kKeysWaitMs);
                return false;
            }
            pump(10);
        }
    }

    bool registered_ = false;
    unsigned int hotkeyVk_ = 0;
};

}  // namespace

std::unique_ptr<Platform> makePlatform() {
    const char* why = nullptr;
    if (qEnvironmentVariable("REVISA_TRAY_PLATFORM") == QLatin1String("clipboard")) why = "REVISA_TRAY_PLATFORM=clipboard";
    else if (QGuiApplication::platformName() != QLatin1String("windows")) why = "Qt platform is not windows";
    if (why) {
        qInfo("platform: clipboard (%s)", why);
        return makeClipboardOnlyPlatform();
    }
    qInfo("platform: win32");
    return std::make_unique<Win32Platform>();
}

}  // namespace revisa::tray
