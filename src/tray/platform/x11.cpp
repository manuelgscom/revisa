// X11 platform: XGrabKey on the root window, PRIMARY
// (or a simulated Ctrl+C) to read, _NET_ACTIVE_WINDOW + a simulated Ctrl+V to paste. XTest is
// dlopen'ed, so the binary starts without libXtst; without it there is no Ctrl+C fallback and
// paste() leaves the text in the clipboard. makePlatform() picks this or the clipboard-only one.

#include "tray/platform/keys_linux.hpp"
#include "tray/platform/platform.hpp"

#include <QAbstractNativeEventFilter>
#include <QClipboard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>

#include <functional>
#include <initializer_list>
#include <vector>

#include <dlfcn.h>
#include <xcb/xcb.h>

// Xlib last: it defines macros (None, Bool, Status, KeyPress...) that clash with Qt names.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

namespace revisa::tray {

namespace {

using FakeKeyFn = int (*)(Display*, unsigned int /*keycode*/, int /*Bool is_press*/, unsigned long /*delay*/);

constexpr unsigned int kModMask = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;
constexpr int kKeysWaitMs = 500;      // modifiers of the hotkey still held
constexpr int kCopyWaitMs = 300;      // Ctrl+C fallback: wait for the clipboard to change
constexpr int kActivateWaitMs = 300;  // window manager honouring _NET_ACTIVE_WINDOW
constexpr int kSettleMs = 80;         // after activating, before Ctrl+V
constexpr int kRestoreMs = 1500;      // after Ctrl+V, restore the previous clipboard (slow targets)
constexpr quint32 kRepeatMs = 300;    // hotkey presses closer than this are auto-repeat

int g_xError = 0;
int recordXError(Display* /*d*/, XErrorEvent* e) {
    g_xError = e->error_code;
    return 0;
}

// Catches the X errors of the calls made in its scope (foreign windows may vanish at any time)
// instead of letting the default handler abort the app. Not reentrant: never nest two.
class ErrorTrap {
public:
    explicit ErrorTrap(Display* d) : d_(d) {
        XSync(d_, False);
        g_xError = 0;
        prev_ = XSetErrorHandler(recordXError);
    }
    ~ErrorTrap() {
        XSync(d_, False);
        XSetErrorHandler(prev_);
    }
    ErrorTrap(const ErrorTrap&) = delete;
    ErrorTrap& operator=(const ErrorTrap&) = delete;
    int error() {
        XSync(d_, False);
        return g_xError;
    }

private:
    Display* d_;
    XErrorHandler prev_ = nullptr;
};

// Runs the Qt event loop for `ms` (no user input), so X selection requests keep being served.
void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

// Only the text formats: every format is a blocking X request to the owner (seconds if it hangs),
// and images or files are not what the user copies before correcting a text.
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

class X11Platform final : public Platform, public QAbstractNativeEventFilter {
public:
    X11Platform(Display* display, unsigned long resourceMask)
        : d_(display), root_(DefaultRootWindow(display)), resourceMask_(resourceMask) {
        netActive_ = XInternAtom(d_, "_NET_ACTIVE_WINDOW", False);
        for (const KeySym s : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Shift_L, XK_Shift_R,
                               XK_Super_L, XK_Super_R, XK_Meta_L, XK_Meta_R, XK_ISO_Level3_Shift}) {
            if (const KeyCode kc = XKeysymToKeycode(d_, s)) modKeys_.push_back(kc);
        }
        int op = 0, ev = 0, err = 0;
        if (XQueryExtension(d_, "XTEST", &op, &ev, &err)) {
            if (void* lib = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL)) {
                fakeKey_ = reinterpret_cast<FakeKeyFn>(dlsym(lib, "XTestFakeKeyEvent"));
            }
        }
        if (!fakeKey_) {
            qWarning("x11: XTest unavailable (libXtst.so.6 or the XTEST extension): no Ctrl+C "
                     "fallback, paste leaves the text in the clipboard");
        }
        qApp->installNativeEventFilter(this);
    }

    ~X11Platform() override {
        if (qApp) qApp->removeNativeEventFilter(this);
        unregisterHotkey();
    }

    QString name() const override { return QStringLiteral("x11"); }

    bool registerHotkey(const QKeySequence& keys, QString* error) override {
        unregisterHotkey();
        const QString shown = keys.toString(QKeySequence::NativeText);
        const x11::Chord chord = x11::chordFromKeys(keys);
        const KeyCode kc = chord.keysym ? XKeysymToKeycode(d_, chord.keysym) : 0;
        if (kc == 0) {
            if (error) *error = QStringLiteral("El atajo %1 no corresponde a ninguna tecla de este teclado.").arg(shown);
            return false;
        }
        bool ok = true;
        {
            ErrorTrap trap(d_);
            for (const unsigned int extra : kLockVariants) {
                XGrabKey(d_, kc, chord.mods | extra, root_, False, GrabModeAsync, GrabModeAsync);
            }
            ok = trap.error() == 0;
        }
        if (!ok) {
            ungrab(kc, chord.mods);
            qInfo("x11: XGrabKey refused for %s", qPrintable(keys.toString()));
            if (error) *error = QStringLiteral("El atajo %1 ya lo usa otro programa. Elige otro en Ajustes.").arg(shown);
            return false;
        }
        hotkeyCode_ = kc;
        hotkeyMods_ = chord.mods;
        havePress_ = false;
        qInfo("x11: hotkey %s grabbed (keycode %u, mods 0x%x)", qPrintable(keys.toString()), static_cast<unsigned>(kc), chord.mods);
        return true;
    }

    void unregisterHotkey() override {
        if (hotkeyCode_ == 0) return;
        ungrab(hotkeyCode_, hotkeyMods_);
        hotkeyCode_ = 0;
        hotkeyMods_ = 0;
    }

    WindowRef activeWindow() override {
        if (const auto w = netActiveWindow(); w && *w) return *w;
        Window focus = 0;
        int revert = 0;
        XGetInputFocus(d_, &focus, &revert);
        if (focus == None || focus == PointerRoot) return 0;
        return topLevel(focus);
    }

    std::optional<QString> readSelection(WindowRef window) override {
        QClipboard* cb = QGuiApplication::clipboard();
        // PRIMARY only if `window`'s app owns it: a stale selection of another app would be
        // corrected and pasted over the user's real selection.
        if (primaryOwnedBy(window)) {
            const QString primary = cb->text(QClipboard::Selection);
            if (!primary.trimmed().isEmpty()) return primary;
        }
        if (!fakeKey_) return std::nullopt;

        // Fallback for apps that do not publish PRIMARY: Ctrl+C, read, restore CLIPBOARD.
        const auto saved = cloneMime(cb->mimeData(QClipboard::Clipboard));
        waitKeysReleased();
        const bool term = isTerminal(window);
        QString text;
        if (waitClipboardChange([&] { term ? sendChord({XK_Control_L, XK_Shift_L}, XK_c) : sendChord({XK_Control_L}, XK_c); })) {
            text = cb->text(QClipboard::Clipboard);
            restoreClipboard(*saved);
        } else {
            qInfo("x11: no selection (PRIMARY empty, Ctrl+C did not change the clipboard)");
        }
        if (text.trimmed().isEmpty()) return std::nullopt;
        return text;
    }

    bool activate(WindowRef window) override {
        const auto w = static_cast<Window>(window);
        if (w == 0) return false;
        if (activeWindow() == window) return true;
        if (netActiveWindow()) {  // EWMH window manager: ask it (source 2 = pager, never refused)
            XEvent ev{};
            ev.xclient.type = ClientMessage;
            ev.xclient.window = w;
            ev.xclient.message_type = netActive_;
            ev.xclient.format = 32;
            ev.xclient.data.l[0] = 2;
            ev.xclient.data.l[1] = CurrentTime;
            {
                ErrorTrap trap(d_);
                XSendEvent(d_, root_, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
            }
            if (waitActive(window, kActivateWaitMs)) return true;
        }
        {
            ErrorTrap trap(d_);
            XRaiseWindow(d_, w);
            XSetInputFocus(d_, w, RevertToParent, CurrentTime);
        }
        if (waitActive(window, 100)) return true;
        qInfo("x11: could not activate window 0x%llx", static_cast<unsigned long long>(window));
        return false;
    }

    bool paste(WindowRef window, const QString& text) override {
        QClipboard* cb = QGuiApplication::clipboard();
        const std::shared_ptr<QMimeData> saved = cloneMime(cb->mimeData(QClipboard::Clipboard));
        cb->setText(text, QClipboard::Clipboard);
        if (!fakeKey_ || !activate(window)) return false;
        pump(kSettleMs);
        waitKeysReleased();
        if (isTerminal(window)) sendChord({XK_Control_L, XK_Shift_L}, XK_v);
        else sendChord({XK_Control_L}, XK_v);
        QTimer::singleShot(kRestoreMs, this, [saved, text] {
            if (QGuiApplication::clipboard()->text(QClipboard::Clipboard) == text) restoreClipboard(*saved);
        });
        return true;
    }

    bool canSimulateInput() const override { return true; }

    bool hasCompositor() const override {
        const QByteArray name = "_NET_WM_CM_S" + QByteArray::number(DefaultScreen(d_));
        const Atom selection = XInternAtom(d_, name.constData(), False);
        return selection != None && XGetSelectionOwner(d_, selection) != None;
    }

    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* /*result*/) override {
        if (hotkeyCode_ == 0 || type != "xcb_generic_event_t") return false;
        const auto* ev = static_cast<const xcb_generic_event_t*>(message);
        if ((ev->response_type & 0x7f) != XCB_KEY_PRESS) return false;
        const auto* kp = reinterpret_cast<const xcb_key_press_event_t*>(ev);
        if (kp->detail != hotkeyCode_ || kp->event != root_ || (kp->state & kModMask) != hotkeyMods_) return false;
        // Holding the hotkey auto-repeats; every press restarts the window, so one hold = one fire.
        const bool repeat = havePress_ && kp->time - lastPress_ < kRepeatMs;
        havePress_ = true;
        lastPress_ = kp->time;
        if (!repeat) QMetaObject::invokeMethod(this, [this] { emit hotkeyPressed(); }, Qt::QueuedConnection);
        return true;
    }

private:
    static constexpr unsigned int kLockVariants[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};

    void ungrab(KeyCode kc, unsigned int mods) {
        ErrorTrap trap(d_);
        for (const unsigned int extra : kLockVariants) XUngrabKey(d_, kc, mods | extra, root_);
    }

    // True if the owner of PRIMARY and `window` belong to the same X client (same resource id
    // base: apps own selections through hidden helper windows, not their top-level). Without the
    // mask it cannot tell and trusts PRIMARY, as before.
    bool primaryOwnedBy(WindowRef window) {
        Window owner = 0;
        {
            ErrorTrap trap(d_);
            owner = XGetSelectionOwner(d_, XA_PRIMARY);
        }
        if (owner == 0) return false;
        if (resourceMask_ == 0) return true;
        const bool same = (owner & ~resourceMask_) == (static_cast<Window>(window) & ~resourceMask_);
        if (!same) {
            qInfo("x11: PRIMARY belongs to another client (owner 0x%lx, window 0x%llx): Ctrl+C instead",
                  owner, static_cast<unsigned long long>(window));
        }
        return same;
    }

    // _NET_ACTIVE_WINDOW of the root; nullopt if there is no EWMH window manager.
    std::optional<Window> netActiveWindow() {
        ErrorTrap trap(d_);
        Atom type = 0;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char* data = nullptr;
        const int rc = XGetWindowProperty(d_, root_, netActive_, 0, 1, False, XA_WINDOW, &type, &format,
                                          &count, &after, &data);
        std::optional<Window> result;
        if (rc == Success && type == XA_WINDOW && format == 32 && count == 1 && data)
            result = *reinterpret_cast<Window*>(data);
        if (data) XFree(data);
        return result;
    }

    // The ancestor of `w` that is a direct child of the root (focus may sit on a child window).
    Window topLevel(Window w) {
        ErrorTrap trap(d_);
        for (int depth = 0; depth < 64 && w; ++depth) {
            Window rootRet = 0, parent = 0;
            Window* children = nullptr;
            unsigned int n = 0;
            if (!XQueryTree(d_, w, &rootRet, &parent, &children, &n)) return 0;
            if (children) XFree(children);
            if (parent == root_ || parent == 0) return w;
            w = parent;
        }
        return w;
    }

    bool waitActive(WindowRef window, int ms) {
        QElapsedTimer t;
        t.start();
        while (activeWindow() != window) {
            if (t.elapsed() >= ms) return false;
            pump(10);
        }
        return true;
    }

    bool isTerminal(WindowRef window) {
        if (window == 0) return false;
        ErrorTrap trap(d_);
        XClassHint hint{};
        if (!XGetClassHint(d_, static_cast<Window>(window), &hint)) return false;
        const QString name = QString::fromLocal8Bit(hint.res_name);
        const QString cls = QString::fromLocal8Bit(hint.res_class);
        if (hint.res_name) XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
        return x11::isTerminalClass(name, cls);
    }

    // Simulated keys reach the grabbing client while the hotkey is held, and held modifiers
    // would add up with ours (Ctrl+Alt+V): wait until modifiers and hotkey key are up.
    bool waitKeysReleased() {
        QElapsedTimer t;
        t.start();
        for (;;) {
            char keys[32] = {};
            XQueryKeymap(d_, keys);
            bool down = false;
            auto isDown = [&](KeyCode kc) { return ((static_cast<unsigned char>(keys[kc / 8]) >> (kc % 8)) & 1) != 0; };
            for (const KeyCode kc : modKeys_) down = down || isDown(kc);
            if (hotkeyCode_) down = down || isDown(hotkeyCode_);
            if (!down) return true;
            if (t.elapsed() >= kKeysWaitMs) {
                qWarning("x11: keys still held after %d ms; simulating anyway", kKeysWaitMs);
                return false;
            }
            pump(10);
        }
    }

    void sendChord(std::initializer_list<KeySym> mods, KeySym key) {
        std::vector<KeyCode> codes;
        for (const KeySym s : mods) codes.push_back(XKeysymToKeycode(d_, s));
        codes.push_back(XKeysymToKeycode(d_, key));
        for (const KeyCode kc : codes)
            if (kc) fakeKey_(d_, kc, True, CurrentTime);
        for (auto it = codes.rbegin(); it != codes.rend(); ++it)
            if (*it) fakeKey_(d_, *it, False, CurrentTime);
        XFlush(d_);
    }

    // Runs `trigger` and waits (<= kCopyWaitMs) for another app to take the CLIPBOARD.
    bool waitClipboardChange(const std::function<void()>& trigger) {
        QEventLoop loop;
        bool changed = false;
        QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, &loop, [&] {
            changed = true;
            loop.quit();
        });
        QTimer::singleShot(kCopyWaitMs, &loop, &QEventLoop::quit);
        trigger();
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        return changed;
    }

    Display* d_;
    Window root_;
    unsigned long resourceMask_;  // bits of an XID that vary inside one client; 0 = unknown
    Atom netActive_ = 0;
    FakeKeyFn fakeKey_ = nullptr;
    std::vector<KeyCode> modKeys_;
    KeyCode hotkeyCode_ = 0;
    unsigned int hotkeyMods_ = 0;
    bool havePress_ = false;
    quint32 lastPress_ = 0;
};

}  // namespace

std::unique_ptr<Platform> makePlatform() {
    const QString forced = qEnvironmentVariable("REVISA_TRAY_PLATFORM");
    const char* why = nullptr;
    if (forced == QLatin1String("clipboard")) why = "REVISA_TRAY_PLATFORM=clipboard";
    else if (forced != QLatin1String("x11") && qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland"))
        why = "XDG_SESSION_TYPE=wayland";
    else if (forced != QLatin1String("x11") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        why = "WAYLAND_DISPLAY is set";
    else if (QGuiApplication::platformName() != QLatin1String("xcb"))
        why = "Qt platform is not xcb";

    Display* display = nullptr;
    unsigned long resourceMask = 0;
    if (!why) {
        auto* native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        display = native ? native->display() : nullptr;
        if (!display) why = "no Xlib display";
        // xcb_get_setup from the libxcb that libX11 already loaded (dlsym: no extra link dependency).
        using GetSetupFn = const xcb_setup_t* (*)(xcb_connection_t*);
        void* xcb = dlopen("libxcb.so.1", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
        auto getSetup = xcb ? reinterpret_cast<GetSetupFn>(dlsym(xcb, "xcb_get_setup")) : nullptr;
        if (native && native->connection() && getSetup)
            if (const xcb_setup_t* setup = getSetup(native->connection())) resourceMask = setup->resource_id_mask;
        if (display && resourceMask == 0) qWarning("x11: no XID resource mask: PRIMARY is trusted whatever its owner");
    }
    if (why) {
        qInfo("platform: clipboard (%s)", why);
        return makeClipboardOnlyPlatform();
    }
    qInfo("platform: x11");
    return std::make_unique<X11Platform>(display, resourceMask);
}

}  // namespace revisa::tray
