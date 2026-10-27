// ttptt — TIM <-> Picture converter for PlayStation 1
// File: placeholder.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placeholder.h"
#include <QFontMetrics>
#include <QPainter>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QPolygonF>
#include <QVector>
#include <QDateTime>
#include <QtMath>

static inline int textWidth(const QFontMetrics &fm, const QString &s) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return fm.horizontalAdvance(s);
#else
    return fm.width(s);
#endif
}

inline double luma(const QColor &c) {
    return 0.299*c.red() + 0.587*c.green() + 0.114*c.blue();
}

namespace Placeholder {

// =====================================================================
// Кнопки геймпада: 0=△ 1=○ 2=× 3=□
// =====================================================================
void drawPSButton(QPainter &p, const QRectF &r, int type, const QColor &c) {
    p.save();
    p.setBrush(Qt::NoBrush);
    const qreal inset = r.width() * 0.18;
    const QRectF inner = r.adjusted(inset, inset, -inset, -inset);
    QPen pen(c, qMax(2.0, r.width() * 0.06),
             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    const qreal gapFrac = 0.22;
    switch (type) {
    case 0: {
        QPointF v0(inner.center().x(), inner.top());
        QPointF v1(inner.right(), inner.bottom());
        QPointF v2(inner.left(), inner.bottom());
        QPointF gA = v2 + (v1 - v2) * (0.5 - gapFrac * 0.5);
        QPointF gB = v2 + (v1 - v2) * (0.5 + gapFrac * 0.5);
        p.drawLine(v2, v0); p.drawLine(v0, v1);
        p.drawLine(v2, gA); p.drawLine(gB, v1);
        break;
    }
    case 1: {
        const int gapDeg = 45;
        p.drawArc(inner, (90 + gapDeg / 2) * 16, (360 - gapDeg) * 16);
        break;
    }
    case 2: {
        const QPointF ctr = inner.center();
        const qreal hw = inner.width() / 2.0, hh = inner.height() / 2.0;
        const QPointF tl(ctr.x() - hw, ctr.y() - hh), br(ctr.x() + hw, ctr.y() + hh);
        const QPointF tr(ctr.x() + hw, ctr.y() - hh), bl(ctr.x() - hw, ctr.y() + hh);
        QPointF a1 = tl + (br - tl) * (0.5 - gapFrac * 0.5);
        QPointF b1 = tl + (br - tl) * (0.5 + gapFrac * 0.5);
        p.drawLine(tl, a1); p.drawLine(b1, br);
        QPointF a2 = tr + (bl - tr) * (0.5 - gapFrac * 0.5);
        QPointF b2 = tr + (bl - tr) * (0.5 + gapFrac * 0.5);
        p.drawLine(tr, a2); p.drawLine(b2, bl);
        break;
    }
    case 3: {
        const qreal x0 = inner.left(), x1 = inner.right();
        const qreal y0 = inner.top(), y1 = inner.bottom();
        const qreal gHalf = (x1 - x0) * gapFrac * 0.5;
        p.drawLine(QPointF(x0, y0), QPointF(x1, y0));
        p.drawLine(QPointF(x0, y1), QPointF(x1, y1));
        const qreal lc = y0 + (y1 - y0) * 0.72, rc = y0 + (y1 - y0) * 0.28;
        p.drawLine(QPointF(x0, y0), QPointF(x0, lc - gHalf));
        p.drawLine(QPointF(x0, lc + gHalf), QPointF(x0, y1));
        p.drawLine(QPointF(x1, y0), QPointF(x1, rc - gHalf));
        p.drawLine(QPointF(x1, rc + gHalf), QPointF(x1, y1));
        break;
    }
    }
    p.restore();
}

// =====================================================================
// Заглушка отсутствующего изображения
// =====================================================================
QImage makeMissingImage(int w, int h, const QString &imgName, const QVector<QColor> &palette)
{
    w = qMax(1, w);
    h = qMax(1, h);

    QImage img(w, h, QImage::Format_ARGB32);

    QColor bgCol(0, 0, 0);
    if (!palette.isEmpty()) {
        bgCol = palette.first();
        for (const QColor &c : palette)
            if (luma(c) < luma(bgCol)) bgCol = c;
    }

    img.fill(bgCol);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, false);

    QColor textCol(255, 255, 255);
    if (!palette.isEmpty()) {
        textCol = palette.first();
        for (const QColor &c : palette)
            if (luma(c) > luma(textCol)) textCol = c;
    }

    int reservedH = 0;
    if (!palette.isEmpty()) {
        const int stripH = qMin(h, qMax(1, qMin(32, h / 4)));
        const int n = palette.size();
        const int cols = qMin(w, qMax(1, int(qSqrt(qreal(n) * w / stripH) + 0.5)));
        const int rows = qMin(stripH, (n + cols - 1) / cols);

        for (int i = 0; i < n && i < cols * rows; ++i) {
            const int col = i % cols;
            const int row = i / cols;
            const int x0 = col * w / cols;
            const int x1 = (col + 1) * w / cols;
            const int y0 = h - stripH + row * stripH / rows;
            const int y1 = h - stripH + (row + 1) * stripH / rows;
            p.fillRect(x0, y0, x1 - x0, y1 - y0, palette[i]);
        }

        if (h > stripH) {
            p.setPen(textCol);
            p.drawLine(0, h - stripH - 1, w - 1, h - stripH - 1);
        }
        reservedH = stripH;
    }

    const int availW = w - 2;
    const int availH = h - reservedH - 2;

    if (availW < 1 || availH < 1 || qMin(w, h) < 8) {
        p.end();
        return img;
    }

    auto wrap = [](const QString &s, const QFontMetrics &fm, int maxW) {
        QStringList result;
        QString line;
        for (const QChar ch : s) {
            const QString next = line + ch;
            if (!line.isEmpty() && textWidth(fm, next) > maxW) {
                result.append(line);
                line.clear();
            }
            line += ch;
        }
        if (!line.isEmpty()) result.append(line);
        return result;
    };

    const QString dimensions = QString("%1×%2").arg(w).arg(h);

    QStringList chosen;
    QFont chosenFont;
    const int maxFont = qMax(1, qMin(24, availH / 2));

    for (int fs = maxFont; fs >= 1 && chosen.isEmpty(); --fs) {
        QFont font;
        font.setBold(true);
        font.setPixelSize(fs);
        const QFontMetrics fm(font);

        const QStringList headers = { QStringLiteral("NO IMAGE"), QStringLiteral("NO") };
        for (const QString &headerText : headers) {
            QStringList baseLines = wrap(headerText, fm, availW);
            if (!imgName.isEmpty())
                baseLines += wrap(imgName, fm, availW);

            for (int withDimensions = 1; withDimensions >= 0; --withDimensions) {
                QStringList candidate = baseLines;
                if (withDimensions)
                    candidate += wrap(dimensions, fm, availW);

                const int totalH = candidate.size() * fm.height();
                if (!candidate.isEmpty() && totalH <= availH) {
                    chosen = candidate;
                    chosenFont = font;
                    break;
                }
            }
            if (!chosen.isEmpty()) break;
        }
    }

    if (!chosen.isEmpty()) {
        p.setFont(chosenFont);
        p.setPen(textCol);
        const QFontMetrics fm(chosenFont);
        int y = 1;
        for (const QString &line : chosen) {
            p.drawText(QRect(1, y, availW, fm.height()),
                       Qt::AlignHCenter | Qt::AlignVCenter, line);
            y += fm.height();
        }
    }

    p.end();
    return img;
}

// =====================================================================
// Утилиты
// =====================================================================
namespace {

struct Rng {
    quint32 s;
    explicit Rng(quint32 seed) : s(seed ? seed : 1) {}
    quint32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int a, int b) { return a + int(next() % quint32(b - a + 1)); }
};

quint32 seedFrom(const QSize &sz) {
    return quint32(sz.width() * 73856093u) ^
           quint32(sz.height() * 19349663u) ^
           quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
}

constexpr double kPi = 3.14159265358979323846;

// ---------- Фон ----------
enum class BgPattern { Plain, VramGrid, DiagStripes, TiledButtons,
                       DotsGrid, HexGrid, Blocky, ScanBlocks };

void drawBgPattern(QPainter &p, const QSize &sz, BgPattern pat) {
    p.fillRect(QRect(0, 0, sz.width(), sz.height()), QColor(12, 15, 22));
    switch (pat) {
    case BgPattern::Plain: break;
    case BgPattern::VramGrid: {
        const int cell = 16;
        p.setPen(QPen(QColor(60, 90, 140, 40), 1));
        for (int x = 0; x < sz.width(); x += cell) p.drawLine(x, 0, x, sz.height());
        for (int y = 0; y < sz.height(); y += cell) p.drawLine(0, y, sz.width(), y);
        p.setPen(QPen(QColor(80, 120, 180, 70), 1));
        for (int x = 0; x < sz.width(); x += cell * 8) p.drawLine(x, 0, x, sz.height());
        break;
    }
    case BgPattern::DiagStripes: {
        p.setPen(QPen(QColor(55, 70, 105, 35), 1));
        const int step = qMax(8, qMin(sz.width(), sz.height()) / 40);
        for (int i = -sz.height(); i < sz.width(); i += step)
            p.drawLine(i, 0, i + sz.height(), sz.height());
        break;
    }
    case BgPattern::TiledButtons: {
        p.save();
        p.setOpacity(0.06);
        const int tile = 96;
        for (int y = -24; y < sz.height(); y += tile)
            for (int x = -24; x < sz.width(); x += tile) {
                int type = (((x / tile) + (y / tile)) % 4 + 4) % 4;
                drawPSButton(p, QRectF(x + 24, y + 24, 48, 48), type,
                             QColor(200, 210, 230));
            }
        p.restore();
        break;
    }
    case BgPattern::DotsGrid: {
        p.setPen(Qt::NoPen);
        const int step = qMax(12, qMin(sz.width(), sz.height()) / 20);
        p.setBrush(QColor(85, 105, 145, 70));
        for (int y = step / 2; y < sz.height(); y += step)
            for (int x = step / 2; x < sz.width(); x += step)
                p.drawEllipse(QPointF(x, y), 1.2, 1.2);
        break;
    }
    case BgPattern::HexGrid: {
        p.setPen(QPen(QColor(60, 90, 140, 35), 1));
        const int R = qMax(8, qMin(sz.width(), sz.height()) / 22);
        const double hStep = R * 1.5, vStep = R * qSqrt(3.0);
        for (int col = 0; col * hStep < sz.width() + R; ++col) {
            const double cx = col * hStep;
            const double yoff = (col % 2) * vStep / 2;
            for (int row = 0; row * vStep < sz.height() + R; ++row) {
                const double cy = row * vStep + yoff;
                QPolygonF hex;
                for (int k = 0; k < 6; ++k) {
                    const double a = kPi / 6 + k * kPi / 3;
                    hex << QPointF(cx + R * qCos(a), cy + R * qSin(a));
                }
                p.drawPolygon(hex);
            }
        }
        break;
    }
    case BgPattern::Blocky: {
        const int cell = qMax(8, qMin(sz.width(), sz.height()) / 16);
        for (int y = 0; y < sz.height(); y += cell)
            for (int x = 0; x < sz.width(); x += cell) {
                const int cx = x / cell, cy = y / cell;
                const int v = (cx * 17 + cy * 31) & 0xFF;
                const int r = 25 + (v & 0x1F);
                const int g = 25 + ((v >> 3) & 0x1F);
                const int b = 45 + ((v >> 5) & 0x3F);
                p.fillRect(x, y, cell, cell, QColor(r, g, b, 22));
            }
        break;
    }
    case BgPattern::ScanBlocks: {
        const int bh = qMax(3, sz.height() / 40);
        for (int y = 0; y < sz.height(); y += bh * 2)
            p.fillRect(0, y, sz.width(), bh, QColor(0, 0, 0, 100));
        break;
    }
    }
}

// ---------- Частицы ----------
enum class Particles { None, Dots, Stars, Sparks };

void drawParticles(QPainter &p, const QSize &sz, Particles pat, Rng &rng) {
    if (pat == Particles::None) return;
    p.setPen(Qt::NoPen);
    const int W = qMax(1, sz.width()), H = qMax(1, sz.height());
    switch (pat) {
    case Particles::Dots: {
        p.setBrush(QColor(150, 180, 230, 90));
        for (int i = 0; i < 40; ++i) {
            const quint32 r = rng.next();
            const int px = int(r % quint32(W));
            const int py = int((r >> 10) % quint32(H));
            p.drawEllipse(QPointF(px, py), 1.0, 1.0);
        }
        break;
    }
    case Particles::Stars: {
        p.setPen(QPen(QColor(220, 230, 255, 130), 1));
        for (int i = 0; i < 25; ++i) {
            const quint32 r = rng.next();
            const int px = int(r % quint32(W));
            const int py = int((r >> 10) % quint32(H));
            p.drawLine(px - 2, py, px + 2, py);
            p.drawLine(px, py - 2, px, py + 2);
        }
        break;
    }
    case Particles::Sparks: {
        for (int i = 0; i < 15; ++i) {
            const quint32 r = rng.next();
            const int px = int(r % quint32(W));
            const int py = int((r >> 10) % quint32(H));
            const int len = 6 + int((r >> 18) & 0xF);
            const int dx = ((r >> 2) & 1) ? 1 : -1;
            const int dy = ((r >> 3) & 1) ? 1 : -1;
            p.setPen(QPen(QColor(255, 200, 120, 160), 1));
            p.drawLine(px, py, px + dx * len, py + dy * len);
        }
        break;
    }
    case Particles::None: break;
    }
}

// ---------- Акцент ----------
enum class Accent { None, RadialGlow, LinearTint, CornerGlow };

void drawAccent(QPainter &p, const QSize &sz, Accent a) {
    switch (a) {
    case Accent::None: break;
    case Accent::RadialGlow: {
        QRadialGradient g(sz.width() / 2.0, sz.height() / 2.0,
                          qMax(sz.width(), sz.height()) * 0.6);
        g.setColorAt(0.0, QColor(60, 90, 145, 70));
        g.setColorAt(1.0, QColor(60, 90, 145, 0));
        p.fillRect(QRect(0, 0, sz.width(), sz.height()), g);
        break;
    }
    case Accent::LinearTint: {
        QLinearGradient g(0, 0, sz.width(), 0);
        g.setColorAt(0.0, QColor(40, 55, 115, 90));
        g.setColorAt(1.0, QColor(40, 55, 115, 0));
        p.fillRect(QRect(0, 0, sz.width(), sz.height()), g);
        break;
    }
    case Accent::CornerGlow: {
        QRadialGradient g(0, 0, qMax(sz.width(), sz.height()) * 0.5);
        g.setColorAt(0.0, QColor(80, 120, 200, 90));
        g.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(QRect(0, 0, sz.width(), sz.height()), g);
        break;
    }
    default: break;
    }
}

// ---------- Пост ----------
enum class Post { None, Scanlines, Vignette, Grain };

void drawPost(QPainter &p, const QSize &sz, Post post, Rng &rng) {
    switch (post) {
    case Post::None: break;
    case Post::Scanlines:
        p.setPen(QPen(QColor(0, 0, 0, 150), 1));
        for (int y = 0; y < sz.height(); y += 3) p.drawLine(0, y, sz.width(), y);
        break;
    case Post::Vignette: {
        QRadialGradient g(sz.width() / 2.0, sz.height() / 2.0,
                          qMax(sz.width(), sz.height()) * 0.75);
        g.setColorAt(0.0, QColor(0, 0, 0, 0));
        g.setColorAt(0.55, QColor(0, 0, 0, 0));
        g.setColorAt(1.0, QColor(0, 0, 0, 200));
        p.fillRect(QRect(0, 0, sz.width(), sz.height()), g);
        break;
    }
    case Post::Grain: {
        const int W = qMax(1, sz.width()), H = qMax(1, sz.height());
        const int n = (W * H) / 100;   // заметно меньше зёрен
        p.setPen(Qt::NoPen);
        for (int i = 0; i < n; ++i) {
            const quint32 r = rng.next();
            const int x = int(r % quint32(W));
            const int y = int((r >> 10) % quint32(H));
            p.fillRect(x, y, 1, 1, QColor(255, 255, 255, 35));
        }
        break;
    }
    default: break;
    }
}

// ---------- Рамка ----------
enum class Frame { Sharp, Rounded, CropMarks };

void drawCropMarks(QImage &img) {
    QPainter dp(&img);
    dp.setPen(QPen(QColor(200, 200, 220, 180), 2));
    int L = qMax(6, qMin(img.width(), img.height()) / 20);
    int m = qMax(4, L / 2);
    QRect r = img.rect();
    dp.drawLine(r.left() + m, r.top() + m, r.left() + m + L, r.top() + m);
    dp.drawLine(r.left() + m, r.top() + m, r.left() + m, r.top() + m + L);
    dp.drawLine(r.right() - m, r.top() + m, r.right() - m - L, r.top() + m);
    dp.drawLine(r.right() - m, r.top() + m, r.right() - m, r.top() + m + L);
    dp.drawLine(r.left() + m, r.bottom() - m, r.left() + m + L, r.bottom() - m);
    dp.drawLine(r.left() + m, r.bottom() - m, r.left() + m, r.bottom() - m - L);
    dp.drawLine(r.right() - m, r.bottom() - m, r.right() - m - L, r.bottom() - m);
    dp.drawLine(r.right() - m, r.bottom() - m, r.right() - m, r.bottom() - m - L);
}

void applyRounded(QImage &img) {
    QImage mask(img.size(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    {
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing, true);
        mp.setPen(Qt::NoPen);
        mp.setBrush(Qt::white);
        mp.drawRoundedRect(QRectF(img.rect()), 14, 14);
    }
    QPainter dp(&img);
    dp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    dp.drawImage(0, 0, mask);
}

// ---------- Нижняя полоса ----------
enum class BottomBar { None, Bgr555Bar, TextEgg, Watermark };

void drawBottomBar(QPainter &p, const QSize &sz, BottomBar b) {
    switch (b) {
    case BottomBar::None: break;
    case BottomBar::Bgr555Bar: {
        const int barH = 6;
        const int barY = sz.height() - barH;
        for (int x = 0; x < sz.width(); ++x) {
            int r5 = (x * 31) / qMax(1, sz.width());
            int g5 = 31 - r5;
            int b5 = (r5 + g5) / 2;
            QColor c((r5 << 3) | (r5 >> 2), (g5 << 3) | (g5 >> 2),
                     (b5 << 3) | (b5 >> 2));
            p.setPen(c);
            p.drawLine(x, barY, x, sz.height());
        }
        break;
    }
    case BottomBar::TextEgg: {
        QFont f = p.font();
        f.setPointSize(qBound(7, sz.width() / 70, 9));
        p.setFont(f);
        p.setPen(QColor(90, 100, 120));
        p.drawText(QRect(0, sz.height() - 22, sz.width(), 18),
                   Qt::AlignHCenter | Qt::AlignTop, "PLEASE INSERT CD-ROM");
        break;
    }
    case BottomBar::Watermark: {
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(qBound(6, sz.width() / 90, 8));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 20));
        p.drawText(QRect(0, sz.height() - 20, sz.width() - 6, 16),
                   Qt::AlignRight | Qt::AlignVCenter, "ttptt");
        break;
    }
    }
}

// =====================================================================
// Порча текста
// =====================================================================
QString corruptText(const QString &in, Rng &rng, double rate) {
    static const char symbols[] = "#@!$%&*?<>[]{}~^|\\/+-=_";
    static const char alnum[]   = "abcdefghijklmnopqrstuvwxyz0123456789";
    constexpr int nsym = int(sizeof(symbols)) - 1;
    constexpr int naln = int(sizeof(alnum)) - 1;
    QString out = in;
    for (int i = 0; i < out.size(); ++i) {
        if (out[i] == QChar(' ')) continue;
        const quint32 r = rng.next();
        const double chance = (r & 0xFFFF) / 65536.0;
        if (chance < rate) {
            if ((r >> 16) & 1)
                out[i] = QChar(symbols[int((r >> 17) % nsym)]);
            else
                out[i] = QChar(alnum[int((r >> 17) % naln)]);
        }
    }
    return out;
}

// =====================================================================
// Глитч-событие
// =====================================================================
void glitchEvent(QPainter &p, const QSize &sz, Rng &rng,
                    const QFont &baseFont, const QString &title)
{
    p.fillRect(QRect(0, 0, sz.width(), sz.height()), QColor(22, 8, 30));

    const int nStrips = qMax(4, sz.height() / 20);
    for (int i = 0; i < nStrips; ++i) {
        const int y = rng.range(0, sz.height());
        const int hh = rng.range(2, qMax(3, sz.height() / 12));
        const int off = rng.range(-sz.width() / 8, sz.width() / 8);
        QColor c(rng.range(0, 255), rng.range(0, 60), rng.range(60, 220),
                 rng.range(60, 140));
        p.fillRect(QRect(off, y, sz.width(), hh), c);
    }

    QFont f = baseFont;
    f.setPointSize(qBound(10, sz.width() / 26, 22));
    f.setBold(true);
	f.setItalic(true);
    p.setFont(f);
    QFontMetrics fm(f);
    const int th = fm.height();
    const int ty = (sz.height() - th) / 2;
    const int shift = qBound(3, fm.height() / 10, 6);

    const QString corruptTitle = corruptText(title, rng, 0.2);

    p.setPen(QColor(255, 0, 60, 200));
    p.drawText(QRect(-shift, ty, sz.width(), th),
               Qt::AlignHCenter | Qt::AlignTop, corruptTitle);
    p.setPen(QColor(0, 255, 200, 200));
    p.drawText(QRect(shift, ty, sz.width(), th),
               Qt::AlignHCenter | Qt::AlignTop, corruptTitle);
    p.setPen(QColor(230, 230, 255));
    p.drawText(QRect(0, ty, sz.width(), th),
               Qt::AlignHCenter | Qt::AlignTop, corruptTitle);

    QFont sf = baseFont;
    sf.setPointSize(qBound(7, sz.width() / 60, 10));
    p.setFont(sf);
    p.setPen(QColor(140, 100, 180, 220));
    p.drawText(QRect(0, ty + th + 4, sz.width(), 20),
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("0x%1").arg(rng.next(), 8, 16, QChar('0')).toUpper());

    QRadialGradient g(sz.width() / 2.0, sz.height() / 2.0,
                      qMax(sz.width(), sz.height()) * 0.75);
    g.setColorAt(0.0, QColor(0, 0, 0, 0));
    g.setColorAt(1.0, QColor(0, 0, 0, 220));
    p.fillRect(QRect(0, 0, sz.width(), sz.height()), g);

    p.setPen(Qt::NoPen);
    for (int i = 0; i < 40; ++i) {
        const int x = rng.range(0, sz.width() - 1);
        const int y = rng.range(0, sz.height() - 1);
        p.fillRect(x, y, 1, 1, QColor(255, 255, 255, 200));
    }
}

// =====================================================================
// Контент: кнопки + текст
// =====================================================================
void drawContent(QPainter &p, const QSize &sz, const QFont &baseFont,
                 const QString &title, const QString &subtitle)
{
    int symSize = qBound(24, qMin(sz.width() / 6, sz.height() / 5), 64);
    int gap = qMax(4, symSize / 3);
    int symRowW = symSize * 4 + gap * 3;

    QFont titleFont = baseFont;
    int titlePt = qBound(10, sz.width() / 28, 18);
    titleFont.setPointSize(titlePt);
    titleFont.setBold(true);
    QFont subFont = baseFont;
    int subPt = qBound(8, sz.width() / 52, 11);
    subFont.setPointSize(subPt);

    QFontMetrics tm(titleFont), sm(subFont);
    int titleH = tm.height(), subH = sm.height();
    int spacing1 = qMax(8, symSize / 2);
    int spacing2 = qMax(3, symSize / 8);
    int contentH = symSize + spacing1 + titleH + spacing2 + subH;

    if (contentH > sz.height() - 8) {
        double k = double(sz.height() - 8) / double(contentH);
        symSize = qMax(12, int(symSize * k));
        gap = qMax(2, int(gap * k));
        symRowW = symSize * 4 + gap * 3;
        titlePt = qMax(8, int(titlePt * k));
        subPt = qMax(6, int(subPt * k));
        titleFont.setPointSize(titlePt);
        subFont.setPointSize(subPt);
        tm = QFontMetrics(titleFont);
        sm = QFontMetrics(subFont);
        titleH = tm.height();
        subH = sm.height();
        contentH = symSize + spacing1 + titleH + spacing2 + subH;
    }

    int y = qMax(4, (sz.height() - contentH) / 2);
    const QColor colors[4] = {
        QColor(0, 186, 92), QColor(232, 53, 58),
        QColor(46, 111, 219), QColor(214, 72, 170)
    };
    const int x0 = (sz.width() - symRowW) / 2;
    for (int i = 0; i < 4; ++i) {
        QRectF r(x0 + i * (symSize + gap), y, symSize, symSize);
        drawPSButton(p, r, i, colors[i]);
    }
    y += symSize + spacing1;

    const int availW = sz.width() - 8;
    QString shownTitle = tm.elidedText(title, Qt::ElideRight, availW);
    p.setFont(titleFont);
    p.setPen(QColor(210, 215, 225));
    p.drawText(QRect(0, y, sz.width(), titleH),
               Qt::AlignHCenter | Qt::AlignTop, shownTitle);
    y += titleH + spacing2;

    QString shownSub = sm.elidedText(subtitle, Qt::ElideRight, availW);
    p.setFont(subFont);
    p.setPen(QColor(120, 130, 150));
    p.drawText(QRect(0, y, sz.width(), subH),
               Qt::AlignHCenter | Qt::AlignTop, shownSub);
}

} // anonymous namespace

// =====================================================================
// Точка входа
// =====================================================================
QImage renderPlaceholder(const QSize &sz, const QFont &baseFont,
                         const QString &title, const QString &subtitle)
{
    if (sz.width() < 40 || sz.height() < 40)
        return QImage();

    QImage img(sz, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    Rng rng(seedFrom(sz));

    // 0 = глитч, 1..3 = статичные пресеты. Сумма весов = 24.
    static const int kWeights[4] = { 3, 7, 7, 7 };
    int total = 0;
    for (int w : kWeights) total += w;

    const int pick = int(rng.next() % quint32(total));
    int style = 0, acc = 0;
    for (int i = 0; i < 4; ++i) {
        acc += kWeights[i];
        if (pick < acc) { style = i; break; }
    }

    // 0: глитч
    if (style == 0) {
        glitchEvent(p, sz, rng, baseFont, title);
        p.end();
        return img;
    }

    // 1..3: статичные пресеты
    switch (style) {
    case 1: // Сетка VRAM
        drawBgPattern(p, sz, BgPattern::VramGrid);
        drawParticles(p, sz, Particles::Dots, rng);
        drawPost(p, sz, Post::Scanlines, rng);
        drawBottomBar(p, sz, BottomBar::Bgr555Bar);
        break;
    case 2: // Блоки
        drawBgPattern(p, sz, BgPattern::Blocky);
        drawParticles(p, sz, Particles::Stars, rng);
        drawAccent(p, sz, Accent::RadialGlow);
        drawPost(p, sz, Post::Vignette, rng);
        drawBottomBar(p, sz, BottomBar::Watermark);
        break;
    case 3: // Минимал
        drawBgPattern(p, sz, BgPattern::Plain);
        break;
    }

    drawContent(p, sz, baseFont, title, subtitle);
    p.end();

    // Рамка
    if (style == 2)      applyRounded(img);
    else if (style == 3) drawCropMarks(img);

    return img;
}

} // namespace Placeholder