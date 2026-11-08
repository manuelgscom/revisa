#pragma once
// Icons painted with QPainter (no SVG module, no files): line icons on a 24-unit grid, with
// rounded caps and joins and a 1.75-2 px stroke whatever the size, rendered at the device pixel
// ratio so they stay sharp on HiDPI screens.

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QRectF>

class QPainter;

namespace revisa::tray::icons {

enum class Glyph {
    Check,
    Close,
    Copy,  // clipboard
    Settings,
    Pause,
    Play,
    Info,
    Warning,
    Keyboard,
    Minus,
    Plus,
    Download,
    Power,
};

// Paints `glyph` into `box` (any size; drawn on its 24-unit grid scaled to the box) with a pen of
// `strokePx` device-independent pixels (0 = the default for the box size).
void paint(QPainter& p, Glyph glyph, const QRectF& box, const QColor& color, qreal strokePx = 0);

// A transparent pixmap of `size` x `size` logical pixels at `dpr`.
QPixmap pixmap(Glyph glyph, const QColor& color, int size, qreal dpr);

// An icon with 1x and 2x pixmaps of `size`, the same in every mode (so a disabled menu entry
// keeps its color).
QIcon icon(Glyph glyph, const QColor& color, int size = 16);

// A filled dot (status of the tray menu), centered in a `size` pixmap.
QIcon dot(const QColor& color, int size = 16);

// revisa's own icon from the embedded PNGs, `size` logical pixels at `dpr`.
QPixmap appIcon(int size, qreal dpr);

}  // namespace revisa::tray::icons
