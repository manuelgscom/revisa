// Pure helpers of the Win32 platform (src/tray/platform/keys_win32.*): Qt key sequence to
// RegisterHotKey's virtual key and modifiers, and console detection. Windows only: the whole file
// is empty elsewhere.

#ifdef _WIN32

#include "doctest.h"

#include "tray/platform/keys_win32.hpp"

#include <QKeySequence>
#include <QString>

#include <utility>

#include <windows.h>

namespace {

using P = std::pair<unsigned int, unsigned int>;

P p(unsigned int vk, unsigned int mods) { return P(vk, mods); }

P chord(const QKeySequence& keys, revisa::tray::win32::CharToVk charToVk = nullptr) {
    const auto c = revisa::tray::win32::chordFromKeys(keys, charToVk);
    return P(c.vk, c.mods);
}

P chord(const char* text) { return chord(QKeySequence(QString::fromLatin1(text))); }

// A Spanish layout, VkKeyScanW style: ñ is its own key, / is Shift+7, @ is AltGr (Ctrl+Alt)+2.
int spanishLayout(char16_t c) {
    switch (c) {
        case u'ñ': return VK_OEM_3;
        case u'/': return 0x100 | '7';
        case u'@': return 0x600 | '2';
        default: return -1;
    }
}

}  // namespace

TEST_CASE("keys_win32: Qt key sequence to virtual key and MOD_* flags") {
    CHECK(chord("Ctrl+Alt+Space") == p(VK_SPACE, MOD_CONTROL | MOD_ALT));
    CHECK(chord("Ctrl+Shift+A") == p('A', MOD_CONTROL | MOD_SHIFT));
    CHECK(chord("Alt+Z") == p('Z', MOD_ALT));
    CHECK(chord("Meta+5") == p('5', MOD_WIN));
    CHECK(chord("Ctrl+0") == p('0', MOD_CONTROL));
    CHECK(chord("F1") == p(VK_F1, 0));
    CHECK(chord("F24") == p(VK_F24, 0));
    CHECK(chord("Ctrl+F12") == p(VK_F12, MOD_CONTROL));
    CHECK(chord("Ctrl+Return") == p(VK_RETURN, MOD_CONTROL));
    CHECK(chord("Alt+Tab") == p(VK_TAB, MOD_ALT));
    CHECK(chord("Ctrl+Alt+Left") == p(VK_LEFT, MOD_CONTROL | MOD_ALT));
    CHECK(chord("Ctrl+PgUp") == p(VK_PRIOR, MOD_CONTROL));
    CHECK(chord("Ctrl+PgDown") == p(VK_NEXT, MOD_CONTROL));
    CHECK(chord("Ctrl+Ins") == p(VK_INSERT, MOD_CONTROL));
    CHECK(chord("Ctrl+Del") == p(VK_DELETE, MOD_CONTROL));
    CHECK(chord("Ctrl+Home") == p(VK_HOME, MOD_CONTROL));
    CHECK(chord("Ctrl+End") == p(VK_END, MOD_CONTROL));
    CHECK(chord(QKeySequence(Qt::ALT | Qt::Key_Escape)) == p(VK_ESCAPE, MOD_ALT));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Comma)) == p(VK_OEM_COMMA, MOD_CONTROL));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Period)) == p(VK_OEM_PERIOD, MOD_CONTROL | MOD_ALT));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Minus)) == p(VK_OEM_MINUS, MOD_CONTROL));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Plus)) == p(VK_OEM_PLUS, MOD_CONTROL));
    CHECK(chord(QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::KeypadModifier, Qt::Key_5))) == p(VK_NUMPAD5, MOD_CONTROL));
}

TEST_CASE("keys_win32: unusable sequences give vk 0") {
    CHECK(chord(QKeySequence()).first == 0u);
    CHECK(chord("A").first == 0u);      // no modifier: it would steal the key from every app
    CHECK(chord("Space").first == 0u);
    CHECK(chord("Ctrl+K, Ctrl+C").first == 0u);  // more than one chord
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_VolumeUp)).first == 0u);
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Slash)).first == 0u);  // layout key, no lookup
}

TEST_CASE("keys_win32: layout keys go through the VkKeyScanW-style lookup") {
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Ntilde), spanishLayout) == p(VK_OEM_3, MOD_CONTROL));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Slash), spanishLayout) == p('7', MOD_CONTROL | MOD_SHIFT));
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_At), spanishLayout).first == 0u);  // behind AltGr
    CHECK(chord(QKeySequence(Qt::CTRL | Qt::Key_Backslash), spanishLayout).first == 0u);
    CHECK(chord(QKeySequence(Qt::Key_Ntilde), spanishLayout).first == 0u);  // still needs a modifier
    // Fixed keys never ask the layout.
    CHECK(chord(QKeySequence(QStringLiteral("Ctrl+Alt+Space")), spanishLayout) == p(VK_SPACE, MOD_CONTROL | MOD_ALT));
}

TEST_CASE("keys_win32: console window classes") {
    using revisa::tray::win32::isConsoleClass;
    CHECK(isConsoleClass(u"ConsoleWindowClass"));
    CHECK(isConsoleClass(u"CASCADIA_HOSTING_WINDOW_CLASS"));
    CHECK_FALSE(isConsoleClass(u"Notepad"));
    CHECK_FALSE(isConsoleClass(u"Chrome_WidgetWin_1"));
    CHECK_FALSE(isConsoleClass(u""));
}

#endif  // _WIN32
