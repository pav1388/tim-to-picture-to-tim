// ttptt — TIM <-> PNG converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placeholder.h"
#include <QFontMetrics>
#include <QPainter>
#include <QRadialGradient>
#include <QVector>
#include <QDateTime>

namespace PlaceholderArt {
	
// =====================================================================
// Заглушка для отсутствующего PNG в .ttptt-проекте
// =====================================================================
QImage makeMissingTimPng(int w, int h, int globalIdx, int bpp,
						 const QString &pngName)
{
	w = qMax(1, w);
	h = qMax(1, h);
	QImage img(w, h, QImage::Format_ARGB32);
	img.fill(Qt::black);

	const int realBpp = (bpp == 0) ? 4 : bpp * 8;

	QPainter p(&img);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setPen(Qt::white);

	const int ptSize = qBound(4, qMin(w, h) / 20, 24);
	QFont f = p.font();
	f.setPointSize(ptSize);
	f.setBold(true);
	p.setFont(f);

	QString text = QStringLiteral("-NO PNG-\nimg%1\n%2x%3\n%4bpp")
					   .arg(globalIdx).arg(w).arg(h).arg(realBpp);
	if (!pngName.isEmpty())
		text += QStringLiteral("\n") + pngName;

	const QRect r(2, 2, w - 4, h - 4);
	p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, text);
	p.end();
	return img;
}

// =====================================================================
// Геймпад: 0=△ 1=○ 2=× 3=□
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
	case 0: { // △
		QPointF v0(inner.center().x(), inner.top());
		QPointF v1(inner.right(),	 inner.bottom());
		QPointF v2(inner.left(),	  inner.bottom());
		QPointF gA = v2 + (v1 - v2) * (0.5 - gapFrac * 0.5);
		QPointF gB = v2 + (v1 - v2) * (0.5 + gapFrac * 0.5);
		p.drawLine(v2, v0);
		p.drawLine(v0, v1);
		p.drawLine(v2, gA);
		p.drawLine(gB, v1);
		break;
	}
	case 1: { // ○
		const int gapDeg = 45;
		const int start = (90 + gapDeg / 2) * 16;
		const int span  = (360 - gapDeg) * 16;
		p.drawArc(inner, start, span);
		break;
	}
	case 2: { // ×
		const QPointF ctr = inner.center();
		const qreal hw = inner.width()  / 2.0;
		const qreal hh = inner.height() / 2.0;
		const QPointF tl(ctr.x() - hw, ctr.y() - hh);
		const QPointF tr(ctr.x() + hw, ctr.y() - hh);
		const QPointF bl(ctr.x() - hw, ctr.y() + hh);
		const QPointF br(ctr.x() + hw, ctr.y() + hh);
		QPointF a1 = tl + (br - tl) * (0.5 - gapFrac * 0.5);
		QPointF b1 = tl + (br - tl) * (0.5 + gapFrac * 0.5);
		p.drawLine(tl, a1);
		p.drawLine(b1, br);
		QPointF a2 = tr + (bl - tr) * (0.5 - gapFrac * 0.5);
		QPointF b2 = tr + (bl - tr) * (0.5 + gapFrac * 0.5);
		p.drawLine(tr, a2);
		p.drawLine(b2, bl);
		break;
	}
		case 3: { // □
		const qreal x0 = inner.left(), x1 = inner.right();
		const qreal y0 = inner.top(),  y1 = inner.bottom();
		const qreal gHalf = (x1 - x0) * gapFrac * 0.5;
		p.drawLine(QPointF(x0, y0), QPointF(x1, y0));
		p.drawLine(QPointF(x0, y1), QPointF(x1, y1));
		const qreal leftCtr = y0 + (y1 - y0) * 0.72;
		p.drawLine(QPointF(x0, y0), QPointF(x0, leftCtr - gHalf));
		p.drawLine(QPointF(x0, leftCtr + gHalf), QPointF(x0, y1));
		const qreal rightCtr = y0 + (y1 - y0) * 0.28;
		p.drawLine(QPointF(x1, y0), QPointF(x1, rightCtr - gHalf));
		p.drawLine(QPointF(x1, rightCtr + gHalf), QPointF(x1, y1));
		break;
	}
	}
	p.restore();
}

// =====================================================================
// Темы оформления заглушки
// =====================================================================
struct Theme {
	QString name;
	QColor  bg;
	bool rounded		 = false;   // скруглённые углы (альфа-маска)
	bool scanlines	   = false;   // CRT-сканлайны
	bool vignette		= false;   // радиальная виньетка
	bool vramGrid		= false;   // сетка «VRAM» 16px
	bool particles	   = false;   // «BIOS boot» частицы
	bool tiledButtons	= false;   // бледные △○×□ по фону
	bool bgr555Bar	   = false;   // BGR555-рампа внизу
	bool diagonalStripes = false;   // диагональные штрихи (Amiga)
	bool easterEgg	   = false;   // «PLEASE INSERT CD-ROM»
};

static QVector<Theme> buildThemes() {
	QVector<Theme> v;
	{   // 0: CRT
		Theme t; t.name = "CRT"; t.bg = QColor(8, 10, 14);
		t.rounded = t.scanlines = t.vignette = true;
		t.easterEgg = true;
		v.append(t);
	}
	{   // 1: BIOS
		Theme t; t.name = "BIOS"; t.bg = QColor(6, 8, 18);
		t.vramGrid = t.particles = t.vignette = true;
		v.append(t);
	}
	{   // 2: Amiga
		Theme t; t.name = "Amiga"; t.bg = QColor(10, 8, 20);
		t.diagonalStripes = t.scanlines = true;
		v.append(t);
	}
	{   // 3: VRAM
		Theme t; t.name = "VRAM"; t.bg = QColor(12, 14, 22);
		t.vramGrid = t.bgr555Bar = t.rounded = true;
		v.append(t);
	}
	{   // 4: Buttons
		Theme t; t.name = "Buttons"; t.bg = QColor(14, 16, 22);
		t.tiledButtons = t.vignette = true;
		v.append(t);
	}
	{   // 5: Retro
		Theme t; t.name = "Retro"; t.bg = QColor(10, 12, 16);
		t.scanlines = t.vramGrid = t.diagonalStripes = true;
		v.append(t);
	}
	return v;
}

static const QVector<Theme> &themes() {
	static const QVector<Theme> v = buildThemes();
	return v;
}

int themeCount() { return int(themes().size()); }

QString themeName(int index) {
	const auto &v = themes();
	if (index < 0 || index >= v.size()) return QString();
	return v[index].name;
}

// =====================================================================
// Эффекты
// =====================================================================
static void drawScanlines(QPainter &p, const QSize &sz) {
	p.setPen(QPen(QColor(0, 0, 0, 140), 1));
	for (int y = 0; y < sz.height(); y += 3)
		p.drawLine(0, y, sz.width(), y);

	p.setPen(QPen(QColor(255, 255, 255, 18), 1));
	for (int y = 1; y < sz.height(); y += 3)
		p.drawLine(0, y, sz.width(), y);
}

static void drawVignette(QPainter &p, const QSize &sz) {
	QRadialGradient vg(sz.width() / 2.0, sz.height() / 2.0,
					   qMax(sz.width(), sz.height()) * 0.75);
	vg.setColorAt(0.0,  QColor(0, 0, 0, 0));
	vg.setColorAt(0.55, QColor(0, 0, 0, 0));
	vg.setColorAt(1.0,  QColor(0, 0, 0, 190));
	p.fillRect(QRect(0, 0, sz.width(), sz.height()), vg);
}

static void drawVramGrid(QPainter &p, const QSize &sz) {
	p.setPen(QPen(QColor(60, 90, 140, 35), 1));
	const int cell = 16;
	for (int x = 0; x < sz.width();  x += cell) p.drawLine(x, 0, x, sz.height());
	for (int y = 0; y < sz.height(); y += cell) p.drawLine(0, y, sz.width(), y);
	p.setPen(QPen(QColor(80, 120, 180, 55), 1));
	for (int x = 0; x < sz.width(); x += cell * 8) p.drawLine(x, 0, x, sz.height());
}

static void drawParticles(QPainter &p, const QSize &sz) {
	quint32 seed = quint32(sz.width()) * 73856093u
				 ^ quint32(sz.height()) * 19349663u;
	p.setPen(Qt::NoPen);
	for (int i = 0; i < 90; ++i) {
		seed = seed * 1664525u + 1013904223u;
		int px = int((seed >> 8) % quint32(qMax(1, sz.width())));
		seed = seed * 1664525u + 1013904223u;
		int py = int((seed >> 8) % quint32(qMax(1, sz.height())));
		seed = seed * 1664525u + 1013904223u;
		int s = 1 + int((seed >> 8) & 0x3);
		int a = 25 + int((seed >> 4) & 0x50);
		p.setBrush(QColor(150, 180, 230, a));
		p.drawEllipse(QPointF(px, py), s * 0.5, s * 0.5);
	}
}

static void drawTiledButtons(QPainter &p, const QSize &sz) {
	p.save();
	p.setOpacity(0.05);
	const int tile = 96;
	for (int y = -24; y < sz.height(); y += tile) {
		for (int x = -24; x < sz.width(); x += tile) {
			int type = (((x / tile) + (y / tile)) % 4 + 4) % 4;
			QRectF r(x + 24, y + 24, 48, 48);
			drawPSButton(p, r, type, QColor(200, 210, 230));
		}
	}
	p.restore();
}

static void drawBgr555Bar(QPainter &p, const QSize &sz) {
	const int barH = 6;
	const int barY = sz.height() - barH;
	for (int x = 0; x < sz.width(); ++x) {
		int r5 = (x * 31) / qMax(1, sz.width());
		int g5 = 31 - r5;
		int b5 = (r5 + g5) / 2;
		QColor c((r5 << 3) | (r5 >> 2),
				 (g5 << 3) | (g5 >> 2),
				 (b5 << 3) | (b5 >> 2));
		p.setPen(c);
		p.drawLine(x, barY, x, sz.height());
	}
}

static void drawDiagonalStripes(QPainter &p, const QSize &sz) {
	p.setPen(QPen(QColor(50, 65, 100, 30), 1));
	for (int i = -sz.height(); i < sz.width(); i += 12)
		p.drawLine(i, 0, i + sz.height(), sz.height());
}

static void drawEasterEgg(QPainter &p, const QSize &sz, const QFont &baseFont) {
	QFont egg = baseFont;
	egg.setPointSize(qBound(7, sz.width() / 70, 9));
	p.setFont(egg);
	p.setPen(QColor(90, 100, 120));
	p.drawText(QRect(0, sz.height() - 22, sz.width(), 18),
			   Qt::AlignHCenter | Qt::AlignTop,
			   QStringLiteral("PLEASE INSERT CD-ROM"));
}

// =====================================================================
// Контент: кнопки + заголовок + подзаголовок
// =====================================================================
static void drawContent(QPainter &p, const QSize &sz, const QFont &baseFont,
						const QString &title, const QString &subtitle) {
	const int symSize = qBound(28, qMin(sz.width() / 6, sz.height() / 5), 64);
	const int gap	 = symSize / 3;
	const int symRowW = symSize * 4 + gap * 3;

	QFont titleFont = baseFont;
	titleFont.setPointSize(qBound(10, sz.width() / 28, 18));
	titleFont.setBold(true);
	QFont subFont = baseFont;
	subFont.setPointSize(qBound(8, sz.width() / 52, 11));

	const QFontMetrics tm(titleFont);
	const QFontMetrics sm(subFont);

	const int titleH   = tm.height();
	const int subH	 = sm.height();
	const int spacing1 = qMax(10, symSize / 2);
	const int spacing2 = qMax(4,  symSize / 8);
	const int contentH = symSize + spacing1 + titleH + spacing2 + subH;

	int y = (sz.height() - contentH) / 2;
	if (y < 6) y = 6;

	const QColor colors[4] = {
		QColor(  0, 186,  92),  // △ зелёный
		QColor(232,  53,  58),  // ○ красный
		QColor( 46, 111, 219),  // × синий
		QColor(214,  72, 170)   // □ розовый
	};
	const int x0 = (sz.width() - symRowW) / 2;
	for (int i = 0; i < 4; ++i) {
		QRectF r(x0 + i * (symSize + gap), y, symSize, symSize);
		drawPSButton(p, r, i, colors[i]);
	}
	y += symSize + spacing1;

	p.setFont(titleFont);
	p.setPen(QColor(210, 215, 225));
	p.drawText(QRect(0, y, sz.width(), titleH),
			   Qt::AlignHCenter | Qt::AlignTop, title);
	y += titleH + spacing2;

	p.setFont(subFont);
	p.setPen(QColor(120, 130, 150));
	p.drawText(QRect(0, y, sz.width(), subH),
			   Qt::AlignHCenter | Qt::AlignTop, subtitle);
}

// =====================================================================
// Главная точка входа
// =====================================================================
QImage renderPlaceholder(const QSize &sz, const QFont &baseFont,
						 const QString &title, const QString &subtitle,
						 int themeIndex, QString *themeNameOut) {
	if (sz.width() < 40 || sz.height() < 40)
		return QImage();

	const auto &v = themes();
	int idx;
	if (themeIndex >= 0 && themeIndex < v.size()) {
		idx = themeIndex;
	} else {
		static bool seeded = false;
		if (!seeded) { qsrand(uint(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF)); seeded = true; }
		idx = qrand() % int(v.size());
	}
	const Theme &t = v[idx];
	if (themeNameOut) *themeNameOut = t.name;

	QImage img(sz, QImage::Format_ARGB32);
	img.fill(Qt::transparent);

	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	// 1) База
	p.fillRect(img.rect(), t.bg);

	// 2) Задние слои
	if (t.tiledButtons)	drawTiledButtons(p, sz);
	if (t.diagonalStripes) drawDiagonalStripes(p, sz);
	if (t.vramGrid)		drawVramGrid(p, sz);
	if (t.particles)	   drawParticles(p, sz);

	// 3) Контент (кнопки + текст)
	drawContent(p, sz, baseFont, title, subtitle);

	// 4) Передние слои
	if (t.bgr555Bar) drawBgr555Bar(p, sz);
	if (t.scanlines) drawScanlines(p, sz);
	if (t.vignette)  drawVignette(p, sz);
	if (t.easterEgg) drawEasterEgg(p, sz, baseFont);

	p.end();

	// 5) Скругление углов — альфа-маска DestinationIn
	if (t.rounded) {
		QImage maskImg(sz, QImage::Format_ARGB32);
		maskImg.fill(Qt::transparent);
		{
			QPainter mp(&maskImg);
			mp.setRenderHint(QPainter::Antialiasing, true);
			mp.setPen(Qt::NoPen);
			mp.setBrush(Qt::white);
			mp.drawRoundedRect(QRectF(img.rect()), 14, 14);
		}
		QPainter dp(&img);
		dp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		dp.drawImage(0, 0, maskImg);
		dp.end();
	}

	return img;
}

} // namespace PlaceholderArt