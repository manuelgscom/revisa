// Pure helpers of the Win32 platform (platform/win32.cpp), split out so the headless tests can
// check them without sending keys. Windows only.

#pragma once

#include <QKeySequence>
#include <QStringView>

namespace revisa::tray::win32 {

// One global hotkey as RegisterHotKey wants it: a virtual-key code and MOD_ALT / MOD_CONTROL /
// MOD_SHIFT / MOD_WIN (MOD_NOREPEAT is added by the caller). vk == 0 means it cannot be used.
struct Chord {
    unsigned int vk = 0;
    unsigned int mods = 0;
};

// Layout lookup for printable characters outside the fixed table (ñ, /, ç...): VkKeyScanW
// semantics, i.e. low byte = virtual key, high byte bits 1 = Shift, 2 = Ctrl, 4 = Alt, -1 = none.
using CharToVk = int (*)(char16_t);

// The chord of `keys` (Ctrl+Alt+Space -> VK_SPACE, MOD_CONTROL | MOD_ALT). vk == 0 if `keys` is
// empty, has more than one chord, has an unknown key, or has no modifier (except F1-F24: a
// global hotkey on a bare letter would steal it from every app). `charToVk` may be null.
Chord chordFromKeys(const QKeySequence& keys, CharToVk charToVk = nullptr);

// True if the window class belongs to a console (conhost or Windows Terminal), where copy and
// paste are Ctrl+Shift+C / Ctrl+Shift+V (a bare Ctrl+C would interrupt the running program).
bool isConsoleClass(QStringView cls);

}  // namespace revisa::tray::win32
