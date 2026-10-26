// ttptt — TIM <-> Picture converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QColor>
#include <QImage>
#include <QVector>
#include "timformat.h"

namespace Tim {

inline quint16 rgbToBgr555(int r, int g, int b, int stp = 0) {
	return quint16(((stp & 1) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}
inline QRgb bgr555ToRgb(quint16 c) {
	int r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
	return qRgb((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

QVector<QRgb> getClutPalette(const Group &g, int paletteRow = 0);
QVector<QRgb> getClutPaletteRgba(const Group &g, int paletteRow = 0);

QImage renderPaletteImage(const Group &g, int paletteRow = 0);

// alphaStp: -1 = авто (P-mode для 4/8bpp, RGBA для 16bpp),
//		  0  = непрозрачно,
//		  1  = RGBA с STP-альфой
QImage decodeImage(const Group &g, int imageIdx, int paletteRow = 0, int alphaStp = -1);

QByteArray encodeImage(const QImage &img, const Group &g,
					   const BlockHeader &header, int paletteRow = 0);

// Собирает «плоскую» палитру (все строки CLUT подряд), с ограничением.
QVector<QColor> safeClutFlat(const Group &g, int *cw = nullptr, int *ch = nullptr);

QImage makeCheckerboard(int w, int h, int size = 8);

QImage normalizeImageForTim(const QImage &in, int w, int h);

bool applyEncodedToImage(Group &g, int imgIdx,
						 const QImage &img, int paletteRow);

} // namespace Tim