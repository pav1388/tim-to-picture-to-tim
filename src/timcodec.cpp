// ttptt — TIM <-> Picture converter for PlayStation 1
// File: timcodec.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timcodec.h"
#include <QPainter>
#include <cstring>

namespace Tim {

static QVector<quint16> readBgr555Words(const QByteArray &data, int needWords) {
	QVector<quint16> out(needWords);
	const uchar *p = reinterpret_cast<const uchar*>(data.constData());
	int avail = data.size() / 2;
	for (int i = 0; i < needWords; ++i) {
		if (i < avail) out[i] = rd16(p + i * 2);
		else out[i] = 0;
	}
	return out;
}

// Приведение QImage к размерам TIM-блока
QImage normalizeImageForTim(const QImage &in, int w, int h) {
	if (w <= 0 || h <= 0 || in.isNull()) return QImage();
	if (in.width() == w && in.height() == h) return in;
	return in.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

// Кодирование картинки в конкретный image-блок группы
bool applyEncodedToImage(Group &g, int imgIdx,
						 const QImage &img, int paletteRow)
{
	if (imgIdx < 0 || imgIdx >= g.images.size()) return false;
	QByteArray newData = encodeImage(img, g, g.images[imgIdx].header, paletteRow);
	if (newData.isEmpty()) return false;
	g.images[imgIdx].data = newData;
	g.images[imgIdx].header.size = 12 + newData.size();
	return true;
}

QVector<QRgb> getClutPalette(const Group &g, int paletteRow) {
	QVector<QRgb> out;
	if (!g.hasClut || g.clut.isEmpty()) return out;
	int cw = g.clutHeader.widthWords;
	int ch = qMax<int>(1, g.clutHeader.height);
	paletteRow = qBound(0, paletteRow, ch - 1);
	int need = cw * ch;
	QVector<quint16> vals = readBgr555Words(g.clut, need);
	int start = paletteRow * cw;
	for (int i = start; i < start + cw && i < vals.size(); ++i)
		out.append(bgr555ToRgb(vals[i]));
	return out;
}

QVector<QRgb> getClutPaletteRgba(const Group &g, int paletteRow) {
	QVector<QRgb> out;
	if (!g.hasClut || g.clut.isEmpty()) return out;
	int cw = g.clutHeader.widthWords;
	int ch = qMax<int>(1, g.clutHeader.height);
	paletteRow = qBound(0, paletteRow, ch - 1);
	int need = cw * ch;
	QVector<quint16> vals = readBgr555Words(g.clut, need);
	int start = paletteRow * cw;
	for (int i = start; i < start + cw && i < vals.size(); ++i) {
		quint16 c = vals[i];
		int r = c & 0x1F, gg = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
		int a = (c & 0x8000) ? 128 : 255;
		out.append(qRgba((r << 3) | (r >> 2), (gg << 3) | (gg >> 2),
						 (b << 3) | (b >> 2), a));
	}
	return out;
}

QImage renderPaletteImage(const Group &g, int paletteRow) {
	QVector<QRgb> pal = getClutPalette(g, paletteRow);
	if (pal.isEmpty()) return QImage(64, 64, QImage::Format_RGB32);
	QImage img(pal.size() * 4, 32, QImage::Format_RGB32);
	for (int y = 0; y < 32; ++y) {
		QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int i = 0; i < pal.size(); ++i) {
			QRgb c = pal[i];
			int baseX = i * 4;
			line[baseX] = c; line[baseX+1] = c; line[baseX+2] = c; line[baseX+3] = c;
		}
	}
	return img;
}

QImage decodeImage(const Group &g, int imageIdx, int paletteRow, int alphaStp) {
	if (imageIdx < 0 || imageIdx >= g.images.size())
		return renderPaletteImage(g, paletteRow);
	const Image &im = g.images[imageIdx];
	const BlockHeader &h = im.header;
	int bpp = g.bpp;
	int width = widthWordsToPixels(bpp, h.widthWords);
	int height = h.height;
	if (width <= 0 || height <= 0)
		return QImage(qMax(1, width), qMax(1, height), QImage::Format_RGB32);
	const QByteArray &data = im.data;

	// ----- 4bpp / 8bpp -----
	if (bpp == 0 || bpp == 1) {
		QVector<QRgb> palette = getClutPalette(g, paletteRow);
		int nPx = width * height;
		int palSize = (bpp == 0) ? 16 : 256;
		QVector<uchar> idx(nPx, 0);
		
		if (bpp == 0) {
			int nBytes = (nPx + 1) / 2;
			QByteArray raw = data.left(nBytes);
			if (raw.size() < nBytes) raw.append(QByteArray(nBytes - raw.size(), '\0'));
			const uchar *p = reinterpret_cast<const uchar*>(raw.constData());
			int pairs = nPx / 2;
			for (int i = 0; i < pairs; ++i) {
				idx[i * 2] = p[i] & 0x0F;
				idx[i * 2 + 1] = (p[i] >> 4) & 0x0F;
			}
			if (nPx & 1) idx[nPx - 1] = p[pairs] & 0x0F;
		} else {
			int copyLen = qMin(nPx, data.size());
			memcpy(idx.data(), data.constData(), copyLen);
		}

		if (palette.isEmpty()) {
			QImage gray(width, height, QImage::Format_Grayscale8);
			for (int y = 0; y < height; ++y) {
				uchar *line = gray.scanLine(y);
				const uchar *src = idx.constData() + y * width;
				for (int x = 0; x < width; ++x) {
					int v = src[x];
					if (bpp == 0) v = (v << 4) | v;
					line[x] = uchar(v);
				}
			}
			return (alphaStp == 1) ? gray.convertToFormat(QImage::Format_ARGB32) 
								   : gray.convertToFormat(QImage::Format_RGB32);
		}

		if (alphaStp == 1) {
			QVector<QRgb> palRgba = getClutPaletteRgba(g, paletteRow);
			QImage img(width, height, QImage::Format_ARGB32);
			for (int y = 0; y < height; ++y) {
				QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
				const uchar *src = idx.constData() + y * width;
				for (int x = 0; x < width; ++x) {
					int i = src[x];
					line[x] = (i < palRgba.size()) ? palRgba[i] : qRgba(0, 0, 0, 255);
				}
			}
			return img;
		}

		QImage img(width, height, QImage::Format_Indexed8);
		QVector<QRgb> table;
		table.reserve(palSize);
		for (int i = 0; i < palSize; ++i) {
			if (i < palette.size()) table.append(palette[i]);
			else table.append(qRgb(0, 0, 0));
		}
		img.setColorTable(table);
		
		// Прямое копирование индексов в scanLine
		for (int y = 0; y < height; ++y) {
			uchar *line = img.scanLine(y);
			memcpy(line, idx.constData() + y * width, width);
		}
		return img;
	}

	// ----- 16bpp -----
	if (bpp == 2) {
		int nPx = width * height;
		bool useStp = (alphaStp != 0);
		QImage img(width, height, QImage::Format_ARGB32);
		
		int availPx = data.size() / 2;
		const quint16 *src = reinterpret_cast<const quint16*>(data.constData());
		
		for (int y = 0; y < height; ++y) {
			QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = 0; x < width; ++x) {
				int pxIdx = y * width + x;
				quint16 c = (pxIdx < availPx) ? src[pxIdx] : 0;
				int r = c & 0x1F, gg = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
				int a = (useStp && (c & 0x8000)) ? 128 : 255;
				line[x] = qRgba((r << 3) | (r >> 2), (gg << 3) | (gg >> 2), (b << 3) | (b >> 2), a);
			}
		}
		return img;
	}

	// ----- 24bpp -----
	if (bpp == 3) {
		int stride = h.widthWords * 2;
		QImage img(width, height, QImage::Format_ARGB32);
		const uchar *srcData = reinterpret_cast<const uchar*>(data.constData());
		int dataSize = data.size();
		
		for (int y = 0; y < height; ++y) {
			QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
			int start = y * stride;
			for (int x = 0; x < width; ++x) {
				int pos = start + x * 3;
				int r = 0, gg = 0, b = 0;
				if (pos + 2 < dataSize) {
					r = srcData[pos]; gg = srcData[pos + 1]; b = srcData[pos + 2];
				}
				line[x] = qRgb(r, gg, b);
			}
		}
		return img;
	}

	return QImage(qMax(1, width), qMax(1, height), QImage::Format_RGB32);
}

QByteArray encodeImage(const QImage &imgIn, const Group &g,
					   const BlockHeader &header, int paletteRow) {
	int bpp = g.bpp;
	int width = widthWordsToPixels(bpp, header.widthWords);
	int height = header.height;
	if (width <= 0 || height <= 0) return QByteArray();
	
	QImage img = imgIn;
	if (img.size() != QSize(width, height))
		img = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::FastTransformation);

	// ----- 4bpp / 8bpp -----
	if (bpp == 0 || bpp == 1) {
		QVector<QRgb> palette = getClutPalette(g, paletteRow);
		if (palette.isEmpty()) return QByteArray();

		const int maxIdx = (bpp == 0) ? 15 : 255;
		const int nPx	= width * height;
		QVector<uchar> indices(nPx, 0);

		QHash<QRgb, int> colorMap;
		colorMap.reserve(palette.size());
		for (int i = 0; i < palette.size(); ++i)
			if (!colorMap.contains(palette[i])) colorMap.insert(palette[i], i);

		auto nearestIdx = [&palette](QRgb rgb) -> int {
			int bestI = 0, bestD = INT_MAX;
			for (int j = 0; j < palette.size(); ++j) {
				int dr = qRed(palette[j])   - qRed(rgb);
				int dg = qGreen(palette[j]) - qGreen(rgb);
				int db = qBlue(palette[j])  - qBlue(rgb);
				int d  = dr*dr + dg*dg + db*db;
				if (d < bestD) { bestD = d; bestI = j; }
			}
			return bestI;
		};

		auto mapPixel = [&](QRgb rgb) -> uchar {
			auto it = colorMap.constFind(rgb);
			int idx;
			if (it != colorMap.constEnd()) idx = it.value();
			else { idx = nearestIdx(rgb); colorMap.insert(rgb, idx); }
			if (idx > maxIdx) idx = 0;
			return uchar(idx);
		};

		const bool indexed = (img.format() == QImage::Format_Indexed8);
		QImage	   rgba;
		QVector<QRgb> srcTable;
		if (indexed) srcTable = img.colorTable();
		else		 rgba	 = img.convertToFormat(QImage::Format_ARGB32);

		for (int y = 0; y < height; ++y) {
			uchar *dst = indices.data() + y * width;
			const uchar *srcIdx = indexed ? img.constScanLine(y) : nullptr;
			const QRgb  *srcRgb = indexed ? nullptr
				: reinterpret_cast<const QRgb*>(rgba.constScanLine(y));

			for (int x = 0; x < width; ++x) {
				QRgb c;
				if (indexed) {
					const int sidx = srcIdx[x];
					c = (sidx < srcTable.size()) ? srcTable[sidx] : qRgb(0, 0, 0);
					// Сохраняем исходный индекс, если цвет совпал с палитрой TIM
					if (sidx <= maxIdx && sidx < palette.size() &&
						sidx < srcTable.size() &&
						qRed(c)   == qRed(palette[sidx]) &&
						qGreen(c) == qGreen(palette[sidx]) &&
						qBlue(c)  == qBlue(palette[sidx])) {
						dst[x] = uchar(sidx);
						continue;
					}
				} else {
					c = srcRgb[x];
				}

				if (qAlpha(c) < 255) { dst[x] = 0; continue; }
				dst[x] = mapPixel(qRgb(qRed(c), qGreen(c), qBlue(c)));
			}
		}

		if (bpp == 0) {
			const int nBytes = (nPx + 1) / 2;
			QByteArray out(nBytes, '\0');
			uchar *p = reinterpret_cast<uchar*>(out.data());
			const int pairs = nPx / 2;
			for (int i = 0; i < pairs; ++i)
				p[i] = uchar(((indices[i * 2 + 1] & 0x0F) << 4) | (indices[i * 2] & 0x0F));
			if (nPx & 1) p[pairs] = indices[nPx - 1] & 0x0F;
			return out;
		}

		QByteArray out(nPx, '\0');
		memcpy(out.data(), indices.constData(), nPx);
		return out;
	}
	
	// ----- 16bpp -----
	if (bpp == 2) {
		QImage rgba = img.convertToFormat(QImage::Format_ARGB32);
		int nPx = width * height;
		QByteArray out(nPx * 2, '\0');
		quint16 *dst = reinterpret_cast<quint16*>(out.data());
		
		for (int y = 0; y < height; ++y) {
			const QRgb *line = reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
			for (int x = 0; x < width; ++x) {
				QRgb c = line[x];
				int stp = (qAlpha(c) < 255) ? 1 : 0;
				dst[y * width + x] = rgbToBgr555(qRed(c), qGreen(c), qBlue(c), stp);
			}
		}
		return out;
	}

	// ----- 24bpp -----
	if (bpp == 3) {
		int stride = header.widthWords * 2;
		QImage rgb = img.convertToFormat(QImage::Format_RGB32);
		QByteArray out(stride * height, '\0');
		uchar *dstData = reinterpret_cast<uchar*>(out.data());
		
		for (int y = 0; y < height; ++y) {
			const QRgb *line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
			int dstStart = y * stride;
			for (int x = 0; x < width; ++x) {
				QRgb c = line[x];
				int pos = dstStart + x * 3;
				if (pos + 2 < out.size()) {
					dstData[pos]	 = qRed(c);
					dstData[pos + 1] = qGreen(c);
					dstData[pos + 2] = qBlue(c);
				}
			}
		}
		return out;
	}

	return QByteArray();
}

QVector<QColor> safeClutFlat(const Group &g, int *cwOut, int *chOut) {
	QVector<QColor> out;
	if (!g.hasClut || g.clut.isEmpty()) {
		if (cwOut) *cwOut = 0;
		if (chOut) *chOut = 0;
		return out;
	}
	int cw = g.clutHeader.widthWords;
	int ch = qMax<int>(1, g.clutHeader.height);
	qint64 total = qint64(cw) * ch;
	const int MAX_ENTRIES = 65536;
	if (total > MAX_ENTRIES) {
		if (cw > MAX_ENTRIES) { cw = MAX_ENTRIES; ch = 1; }
		else ch = qMax(1, MAX_ENTRIES / cw);
		total = qint64(cw) * ch;
	}
	QVector<quint16> vals = readBgr555Words(g.clut, int(total));
	for (quint16 v : vals) out.append(QColor(bgr555ToRgb(v)));
	if (cwOut) *cwOut = cw;
	if (chOut) *chOut = ch;
	return out;
}

QImage makeCheckerboard(int w, int h, int size) {
	w = qMax(1, w); h = qMax(1, h); size = qMax(1, size);
	QImage img(w, h, QImage::Format_RGB32);
	for (int y = 0; y < h; ++y) {
		QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
		int yBlock = y / size;
		for (int x = 0; x < w; ++x) {
			bool dark = (((x / size) ^ yBlock) & 1) == 0;
			line[x] = dark ? qRgb(140, 140, 140) : qRgb(190, 190, 190);
		}
	}
	return img;
}

} // namespace Tim