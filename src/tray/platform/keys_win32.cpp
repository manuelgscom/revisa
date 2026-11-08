#include "tray/platform/keys_win32.hpp"

#include <QChar>

#include <windows.h>

namespace revisa::tray::win32 {

namespace {

constexpr int kShiftBit = 0x100;  // high byte of a VkKeyScanW result
constexpr int kCtrlBit = 0x200;
constexpr int kAltBit = 0x400;
constexpr unsigned int kVkF1 = VK_F1;
constexpr unsigned int kVkF24 = VK_F24;

// Keys whose virtual-key code does not depend on the keyboard layout. 0 = not in the table.
unsigned int fixedVk(int key, bool keypad) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<unsigned int>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<unsigned int>((keypad ? VK_NUMPAD0 : '0') + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return static_cast<unsigned int>(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Tab:
        case Qt::Key_Backtab: return VK_TAB;
        case Qt::Key_Backspace: return VK_BACK;
        case Qt::Key_Return:
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Pause: return VK_PAUSE;
        case Qt::Key_Print: return VK_SNAPSHOT;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Menu: return VK_APPS;
        // The OEM keys Windows defines "for any country/region"; the rest move with the layout.
        case Qt::Key_Plus: return VK_OEM_PLUS;
        case Qt::Key_Comma: return VK_OEM_COMMA;
        case Qt::Key_Minus: return VK_OEM_MINUS;
        case Qt::Key_Period: return VK_OEM_PERIOD;
        default: return 0;
    }
}

}  // namespace

Chord chordFromKeys(const QKeySequence& keys, CharToVk charToVk) {
    if (keys.count() != 1) return {};
    const QKeyCombination combo = keys[0];
    const int key = static_cast<int>(combo.key());
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    unsigned int mods = 0;
    if (m.testFlag(Qt::ShiftModifier)) mods |= MOD_SHIFT;
    if (m.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
    if (m.testFlag(Qt::AltModifier)) mods |= MOD_ALT;
    if (m.testFlag(Qt::MetaModifier)) mods |= MOD_WIN;

    unsigned int vk = fixedVk(key, m.testFlag(Qt::KeypadModifier));
    // Other printable characters: ask the current layout. Lowercase first, so Ñ looks up the ñ
    // key and not Shift+ñ. Characters behind AltGr (Ctrl+Alt) cannot be a hotkey key.
    if (vk == 0 && charToVk && key > 0x20 && key <= 0xffff) {
        const QChar c = QChar(static_cast<char16_t>(key)).toLower();
        const int r = c.isPrint() ? charToVk(c.unicode()) : -1;
        if (r != -1 && (r & 0xff) != 0 && (r & (kCtrlBit | kAltBit)) == 0) {
            vk = static_cast<unsigned int>(r & 0xff);
            if (r & kShiftBit) mods |= MOD_SHIFT;
        }
    }
    if (vk == 0) return {};
    if (mods == 0 && (vk < kVkF1 || vk > kVkF24)) return {};
    return {vk, mods};
}

bool isConsoleClass(QStringView cls) {
    return cls.compare(u"ConsoleWindowClass", Qt::CaseInsensitive) == 0 ||
           cls.compare(u"CASCADIA_HOSTING_WINDOW_CLASS", Qt::CaseInsensitive) == 0;
}

}  // namespace revisa::tray::win32
