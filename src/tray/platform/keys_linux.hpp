// Pure helpers of the X11 platform (platform/x11.cpp), split out so the headless tests can
// check them without an X server. Linux only.

#pragma once

#include <QKeySequence>
#include <QStringView>

namespace revisa::tray::x11 {

// One global hotkey as X11 sees it: a keysym and a core modifier mask (ShiftMask, ControlMask,
// Mod1Mask = Alt, Mod4Mask = Super). keysym == 0 means the key has no X equivalent.
struct Chord {
    unsigned long keysym = 0;
    unsigned int mods = 0;
};

// The first chord of `keys` (Ctrl+Alt+Space -> XK_space, ControlMask | Mod1Mask). Letters map to
// the lowercase keysym, which XKeysymToKeycode resolves on any layout.
Chord chordFromKeys(const QKeySequence& keys);

// True if the WM_CLASS (res_name, res_class) of a window belongs to a terminal emulator, where
// copy and paste are Ctrl+Shift+C / Ctrl+Shift+V. Case-insensitive, closed list.
bool isTerminalClass(QStringView name, QStringView cls);

}  // namespace revisa::tray::x11
