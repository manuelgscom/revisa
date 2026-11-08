#include "tray/icons.hpp"

#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace revisa::tray::icons {

namespace {

QPointF polar(qreal radius, qreal degrees) {
    const qreal a = degrees * std::numbers::pi / 180.0;
    return {12.0 + radius * std::cos(a), 12.0 + radius * std::sin(a)};
}

// A gear: 8 teeth around a ring, and the hub.
QPainterPath gear() {
    QPainterPath path;
    constexpr qreal kOuter = 10.0;
    constexpr qreal kInner = 7.4;
    constexpr int kTeeth = 8;
    constexpr qreal kStep = 360.0 / kTeeth;
    for (int i = 0; i < kTeeth; ++i) {
        const qreal c = i * kStep - 90.0;
        const QPointF a = polar(kInner, c - 15.0);
        if (i == 0) path.moveTo(a);
        else path.lineTo(a);
        path.lineTo(polar(kOuter, c - 9.0));
        path.lineTo(polar(kOuter, c + 9.0));
        path.lineTo(polar(kInner, c + 15.0));
        // Along the ring to the next tooth.
        const QRectF ring(12.0 - kInner, 12.0 - kInner, 2 * kInner, 2 * kInner);
        path.arcTo(ring, -(c + 15.0), -(kStep - 30.0));
    }
    path.closeSubpath();
    path.addEllipse(QPointF(12, 12), 3.0, 3.0);
    return path;
}

void dotAt(QPainter& p, QPointF at, qreal radius, const QColor& color) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(at, radius, radius);
    p.restore();
}

}  // namespace

void paint(QPainter& p, Glyph glyph, const QRectF& box, const QColor& color, qreal strokePx) {
    const qreal size = std::min(box.width(), box.height());
    if (size <= 0) return;
    if (strokePx <= 0) strokePx = std::clamp(size / 24.0 * 2.0, 1.75, 2.0);
    const qreal scale = size / 24.0;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(box.center().x() - size / 2, box.center().y() - size / 2);
    p.scale(scale, scale);
    QPen pen(color, strokePx / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (glyph) {
        case Glyph::Check: {
            QPainterPath path;
            path.moveTo(5.0, 12.5);
            path.lineTo(9.8, 17.3);
            path.lineTo(19.0, 7.0);
            p.drawPath(path);
            break;
        }
        case Glyph::Close:
            p.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
            p.drawLine(QPointF(17.5, 6.5), QPointF(6.5, 17.5));
            break;
        case Glyph::Copy: {
            p.drawRoundedRect(QRectF(8.5, 2.5, 7, 4), 1.2, 1.2);
            QPainterPath body;
            body.moveTo(15.5, 4.5);
            body.lineTo(17.0, 4.5);
            body.arcTo(QRectF(15.0, 4.5, 4, 4), 90, -90);
            body.lineTo(19.0, 19.5);
            body.arcTo(QRectF(15.0, 17.5, 4, 4), 0, -90);
            body.lineTo(7.0, 21.5);
            body.arcTo(QRectF(5.0, 17.5, 4, 4), 270, -90);
            body.lineTo(5.0, 6.5);
            body.arcTo(QRectF(5.0, 4.5, 4, 4), 180, -90);
            body.lineTo(8.5, 4.5);
            p.drawPath(body);
            p.drawLine(QPointF(9.0, 12.0), QPointF(15.0, 12.0));
            p.drawLine(QPointF(9.0, 16.0), QPointF(13.0, 16.0));
            break;
        }
        case Glyph::Settings:
            p.drawPath(gear());
            break;
        case Glyph::Pause:
            p.drawRoundedRect(QRectF(6.5, 5.0, 3.5, 14.0), 1.0, 1.0);
            p.drawRoundedRect(QRectF(14.0, 5.0, 3.5, 14.0), 1.0, 1.0);
            break;
        case Glyph::Play: {
            QPainterPath path;
            path.moveTo(7.5, 5.0);
            path.lineTo(18.5, 12.0);
            path.lineTo(7.5, 19.0);
            path.closeSubpath();
            p.drawPath(path);
            break;
        }
        case Glyph::Info:
            p.drawEllipse(QPointF(12, 12), 9.5, 9.5);
            p.drawLine(QPointF(12.0, 11.0), QPointF(12.0, 16.5));
            dotAt(p, QPointF(12.0, 7.75), 1.25, color);
            break;
        case Glyph::Warning: {
            QPainterPath path;
            path.moveTo(12.0, 3.5);
            path.lineTo(21.5, 19.5);
            path.lineTo(2.5, 19.5);
            path.closeSubpath();
            p.drawPath(path);
            p.drawLine(QPointF(12.0, 9.5), QPointF(12.0, 13.5));
            dotAt(p, QPointF(12.0, 16.5), 1.25, color);
            break;
        }
        case Glyph::Keyboard: {
            p.drawRoundedRect(QRectF(2.5, 6.0, 19.0, 12.0), 2.5, 2.5);
            for (const qreal x : {6.5, 10.0, 13.5, 17.0}) dotAt(p, QPointF(x, 10.0), 1.1, color);
            p.drawLine(QPointF(8.0, 14.25), QPointF(16.0, 14.25));
            break;
        }
        case Glyph::Minus:
            p.drawLine(QPointF(6.0, 12.0), QPointF(18.0, 12.0));
            break;
        case Glyph::Plus:
            p.drawLine(QPointF(6.0, 12.0), QPointF(18.0, 12.0));
            p.drawLine(QPointF(12.0, 6.0), QPointF(12.0, 18.0));
            break;
        case Glyph::Download: {
            p.drawLine(QPointF(12.0, 4.0), QPointF(12.0, 15.0));
            QPainterPath head;
            head.moveTo(7.0, 10.5);
            head.lineTo(12.0, 15.5);
            head.lineTo(17.0, 10.5);
            p.drawPath(head);
            p.drawLine(QPointF(5.0, 19.5), QPointF(19.0, 19.5));
            break;
        }
        case Glyph::Power: {
            p.drawArc(QRectF(4.0, 4.5, 16.0, 16.0), 60 * 16, -300 * 16);
            p.drawLine(QPointF(12.0, 3.0), QPointF(12.0, 11.5));
            break;
        }
    }
    p.restore();
}

QPixmap pixmap(Glyph glyph, const QColor& color, int size, qreal dpr) {
    if (dpr <= 0) dpr = 1;
    const int px = static_cast<int>(std::lround(size * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    paint(p, glyph, QRectF(0, 0, size, size), color);
    p.end();
    return pm;
}

QIcon icon(Glyph glyph, const QColor& color, int size) {
    QIcon out;
    for (const qreal dpr : {1.0, 2.0}) {
        const QPixmap pm = pixmap(glyph, color, size, dpr);
        for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected})
            out.addPixmap(pm, mode);
    }
    return out;
}

QIcon dot(const QColor& color, int size) {
    QIcon out;
    for (const qreal dpr : {1.0, 2.0}) {
        const int px = static_cast<int>(std::lround(size * dpr));
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(dpr);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const qreal r = size * 0.22;
        p.drawEllipse(QPointF(size / 2.0, size / 2.0), r, r);
        p.end();
        for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected})
            out.addPixmap(pm, mode);
    }
    return out;
}

QPixmap appIcon(int size, qreal dpr) {
    if (dpr <= 0) dpr = 1;
    const int px = static_cast<int>(std::lround(size * dpr));
    int best = 256;
    for (const int s : {16, 22, 24, 32, 48, 64, 128, 256}) {
        if (s >= px) {
            best = s;
            break;
        }
    }
    QPixmap source(QStringLiteral(":/revisa/revisa-%1.png").arg(best));
    if (source.isNull()) {
        // Resources not linked in (should not happen): a plain accent square.
        source = QPixmap(px, px);
        source.fill(QColor(0x1f, 0x6f, 0xeb));
    }
    QPixmap out = source.width() == px ? source
                                       : source.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    out.setDevicePixelRatio(dpr);
    return out;
}

}  // namespace revisa::tray::icons
