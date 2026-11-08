#include "tray/platform/keys_linux.hpp"

#include <array>

// X headers last: they define macros (None, KeyPress...) that clash with Qt names.
#include <X11/X.h>
#include <X11/keysym.h>

namespace revisa::tray::x11 {

namespace {

unsigned long keysymForQtKey(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<unsigned long>(key - Qt::Key_A) + XK_a;
    // Printable ASCII and Latin-1: Qt key codes and X keysyms share the code point.
    if (key >= 0x20 && key <= 0x7e) return static_cast<unsigned long>(key);
    if (key >= 0xc0 && key <= 0xde && key != 0xd7) return static_cast<unsigned long>(key + 0x20);
    if (key >= 0xa0 && key <= 0xff) return static_cast<unsigned long>(key);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) return XK_F1 + static_cast<unsigned long>(key - Qt::Key_F1);
    switch (key) {
        case Qt::Key_Escape: return XK_Escape;
        case Qt::Key_Tab: return XK_Tab;
        case Qt::Key_Backtab: return XK_ISO_Left_Tab;
        case Qt::Key_Backspace: return XK_BackSpace;
        case Qt::Key_Return: return XK_Return;
        case Qt::Key_Enter: return XK_KP_Enter;
        case Qt::Key_Insert: return XK_Insert;
        case Qt::Key_Delete: return XK_Delete;
        case Qt::Key_Pause: return XK_Pause;
        case Qt::Key_Print: return XK_Print;
        case Qt::Key_Home: return XK_Home;
        case Qt::Key_End: return XK_End;
        case Qt::Key_Left: return XK_Left;
        case Qt::Key_Up: return XK_Up;
        case Qt::Key_Right: return XK_Right;
        case Qt::Key_Down: return XK_Down;
        case Qt::Key_PageUp: return XK_Prior;
        case Qt::Key_PageDown: return XK_Next;
        case Qt::Key_Menu: return XK_Menu;
        default: return 0;
    }
}

}  // namespace

Chord chordFromKeys(const QKeySequence& keys) {
    if (keys.isEmpty()) return {};
    const QKeyCombination combo = keys[0];
    const unsigned long keysym = keysymForQtKey(static_cast<int>(combo.key()));
    if (keysym == 0) return {};
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    unsigned int mods = 0;
    if (m & Qt::ShiftModifier) mods |= ShiftMask;
    if (m & Qt::ControlModifier) mods |= ControlMask;
    if (m & Qt::AltModifier) mods |= Mod1Mask;
    if (m & Qt::MetaModifier) mods |= Mod4Mask;
    return {keysym, mods};
}

bool isTerminalClass(QStringView name, QStringView cls) {
    static constexpr std::array<QStringView, 37> kTerminals = {
        u"gnome-terminal", u"gnome-terminal-server", u"konsole", u"org.kde.konsole", u"xterm",
        u"uxterm", u"alacritty", u"kitty", u"xfce4-terminal", u"mate-terminal", u"tilix",
        u"com.gexperts.tilix", u"terminator", u"lxterminal", u"qterminal", u"wezterm",
        u"org.wezfurlong.wezterm", u"foot", u"footclient", u"st", u"st-256color", u"urxvt",
        u"rxvt", u"x-terminal-emulator", u"guake", u"tilda", u"terminology", u"ptyxis",
        u"org.gnome.ptyxis", u"kgx", u"org.gnome.console", u"cosmic-term",
        u"com.system76.cosmicterm", u"eterm", u"sakura", u"terminix", u"yakuake"};
    for (const QStringView t : kTerminals) {
        if (name.compare(t, Qt::CaseInsensitive) == 0 || cls.compare(t, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

}  // namespace revisa::tray::x11
