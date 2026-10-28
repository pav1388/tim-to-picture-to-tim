// ttptt — TIM <-> Picture converter for PlayStation 1
// File: timformat.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timformat.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace Tim {

int widthWordsToPixels(int bpp, int words) {
	switch (bpp) {
	case 0: return words * 4;
	case 1: return words * 2;
	case 2: return words;
	case 3: return (words * 2) / 3;
	default: return words;
	}
}
int pixelsToWords(int bpp, int px) {
	switch (bpp) {
	case 0: return (px + 3) / 4;
	case 1: return (px + 1) / 2;
	case 2: return px;
	case 3: return (px * 3 + 1) / 2;
	default: return px;
	}
}

static void warn(Group *g, const QString &msg)
{
    for (int i = 0; i < g->warnings.size(); ++i) {
        const QString &cur = g->warnings[i];
        if (cur == msg || cur.startsWith(msg + " (×")) {
            int n = 2;
            if (cur.startsWith(msg + " (×")) {
                const int start = msg.length() + 3;
                const int close = cur.indexOf(')', start);
                if (close > start) n = cur.mid(start, close - start).toInt() + 1;
            }
            g->warnings[i] = QString("%1 (×%2)").arg(msg).arg(n);
            return;
        }
    }
    if (g->warnings.size() >= 32) return;
    g->warnings << msg;
}

static bool parseGroup(const QByteArray &data, int offset, int *newOffset,
					   bool checkSignature, Group *out) {
	const int len = data.size();
	int start = offset;
	if (offset + 8 > len) return false;

	if (checkSignature) {
		// if (!data.startsWith(QByteArray("\x10\x00\x00\x00", 4))) return false;
        quint32 magic = rd32(reinterpret_cast<const uchar*>(data.constData() + offset));
        if (magic != 0x10) return false;
        quint32 flags = rd32(reinterpret_cast<const uchar*>(data.constData() + offset + 4));
        offset += 8;
        out->flags = flags;
	} else {
		if (data.mid(offset, 4) == QByteArray("\x10\x00\x00\x00", 4)) {
			out->flags = rd32(reinterpret_cast<const uchar*>(data.constData() + offset + 4));
			offset += 8;
		} else {
			out->flags = rd32(reinterpret_cast<const uchar*>(data.constData() + offset));
			offset += 4;
			int bpp = out->flags & 0x7;
			if (bpp > 3) return false;
		}
	}

	out->bpp = out->flags & 0x7;
	out->hasClut = (out->flags & 0x8) != 0;
	out->hasImage = (out->flags & 0x10) != 0;
	out->startOffset = start;

	// ----- CLUT -----
	if (out->hasClut) {
		if (offset + 12 > len) {
			warn(out, QStringLiteral("Обрезан заголовок CLUT"));
			out->clutHeader.size = 12;
			out->clutHeader.x = 0;
			out->clutHeader.y = 0;
			out->clutHeader.widthWords = 16;
			out->clutHeader.height = 1;
			out->clut = QByteArray();
			offset = len;
		} else {
			const uchar *p = reinterpret_cast<const uchar*>(data.constData() + offset);
			quint32 size = rd32(p);
			quint16 cx = rd16(p + 4), cy = rd16(p + 6);
			quint16 cw = rd16(p + 8), ch = rd16(p + 10);
			if (size < 12 || size > 1024 * 1024) {
				warn(out, QStringLiteral("Неверный размер CLUT"));
				size = 12; cw = qMin<quint16>(cw, 256); ch = qMin<quint16>(ch, 16);
			}
			offset += 12;
			int declared = int(size) - 12;
			if (int(cw) * int(ch) * 2 > declared && declared >= 0) {
				int maxE = qMax(1, declared / 2);
				if (int(cw) > maxE) { cw = quint16(qMin<int>(cw, maxE)); ch = 1; }
				else ch = quint16(qMax(1, qMin<int>(ch, maxE / qMax(1, int(cw)))));
				warn(out, QStringLiteral("CLUT: cw*ch не соответствует size"));
			}
			int need = int(size) - 12;
			int avail = len - offset;
			out->clutHeader.size = size;
			out->clutHeader.x = cx;
			out->clutHeader.y = cy;
			out->clutHeader.widthWords = cw;
			out->clutHeader.height = ch;
			if (avail < need) {
				warn(out, QStringLiteral("CLUT обрезан"));
				out->clut = data.mid(offset);
				offset = len;
			} else {
				out->clut = data.mid(offset, need);
				offset += need;
			}
		}
	}

	// ----- IMAGE -----
	int imgCount = 0, scanIters = 0;
	while (offset + 12 <= len && imgCount < 100 && scanIters < 200) {
		++scanIters;
		if (data.mid(offset, 4) == QByteArray("\x10\x00\x00\x00", 4)) break;
		const uchar *p = reinterpret_cast<const uchar*>(data.constData() + offset);
		quint32 size = rd32(p);
		quint16 ix = rd16(p + 4), iy = rd16(p + 6);
		quint16 iw = rd16(p + 8), ih = rd16(p + 10);

		if (size < 12 || size > 100u * 1024 * 1024) {
			warn(out, QStringLiteral("Неверный размер блока image"));
			offset += 12;
			continue;
		}
		if (iw == 0 || ih == 0 || iw > 2048 || ih > 2048 ||
			quint32(iw) * 2 * ih > 4u * 1024 * 1024) {
			warn(out, QStringLiteral("Нереалистичные размеры image"));
			int remaining = len - offset;
			offset += (int(size) >= 12 && int(size) <= remaining) ? int(size) : 12;
			continue;
		}
		int expected = int(iw) * 2 * int(ih);
		int declared = int(size) - 12;
		if (declared > 50 * 1024 * 1024) {
			warn(out, QStringLiteral("Слишком большой размер данных"));
			int remaining = len - offset;
			offset += (int(size) >= 12 && int(size) <= remaining) ? int(size) : 12;
			continue;
		}
		if (qAbs(declared - expected) > 4) {
			warn(out, QStringLiteral("Несоответствие размера image"));
		}
		int avail = len - offset - 12;
		int dataLen = qMin(declared, avail);
		if (dataLen <= 0) {
			warn(out, QStringLiteral("Нет данных image"));
			break;
		}

		Image im;
		im.header.size = size;
		im.header.x = ix;
		im.header.y = iy;
		im.header.widthWords = iw;
		im.header.height = ih;
		im.data = data.mid(offset + 12, dataLen);
		out->images.append(im);

		int advance = qMax<int>(size, 12 + dataLen);
		offset += advance;
		++imgCount;
		if (dataLen < declared) {
			warn(out, QStringLiteral("Данные image обрезаны"));
			break;
		}
	}

	if (out->images.isEmpty() && out->hasImage)
		warn(out, QStringLiteral("has_image=1, но image не найден"));
	if (!out->images.isEmpty() && !out->hasImage)
		warn(out, QStringLiteral("Изображение есть, но has_image=0"));

	// if (offset < len && data.mid(offset, 4) != QByteArray("\x10\x00\x00\x00", 4)) {
		// out->extra = data.mid(offset);
		// offset = len;
	// }
	if (offset < len && data.mid(offset, 4) != QByteArray("\x10\x00\x00\x00", 4)) {
		int nextMagic = data.indexOf(QByteArray("\x10\x00\x00\x00", 4), offset);
		if (nextMagic > offset) {
			out->extra = data.mid(offset, nextMagic - offset);
			offset = nextMagic;
		} else {
			out->extra = data.mid(offset);
			offset = len;
		}
	}

	*newOffset = offset;
	return true;
}

QVector<Group> parseAll(const QByteArray &data) {
	QVector<Group> groups;
	if (data.isEmpty()) return groups;

	int idx = data.indexOf(QByteArray("\x10\x00\x00\x00", 4));
	if (idx < 0) return groups;
	int offset = idx;

	const QByteArray filePrefix = (idx > 0) ? data.left(idx) : QByteArray();

	int iters = 0;
	while (offset < data.size() && groups.size() < 100 && iters < 2000) {
		++iters;
		if (data.mid(offset, 4) != QByteArray("\x10\x00\x00\x00", 4)) {
			int next = data.indexOf(QByteArray("\x10\x00\x00\x00", 4), offset);
			if (next < 0) break;
			offset = next;
			continue;
		}
		Group g;
		int newOff = offset;
		if (parseGroup(data, offset, &newOff, true, &g)) {
			groups.append(g);
			offset = newOff;
		} else {
			int next = data.indexOf(QByteArray("\x10\x00\x00\x00", 4), offset + 4);
			if (next < 0) break;
			offset = next;
		}
	}

	if (!groups.isEmpty() && !filePrefix.isEmpty())
		groups.first().prefix = filePrefix;

	return groups;
}

static void appendHeader(QByteArray &out, const BlockHeader &h) {
	uchar buf[12];
	wr32(buf, h.size); wr16(buf + 4, h.x); wr16(buf + 6, h.y);
	wr16(buf + 8, h.widthWords); wr16(buf + 10, h.height);
	out.append(reinterpret_cast<const char*>(buf), 12);
}

QByteArray rebuildGroup(const Group &g) {
	QByteArray out;
	uchar mag[8];
	wr32(mag, 0x10); wr32(mag + 4, g.flags);
	out.append(reinterpret_cast<const char*>(mag), 8);

	if (g.hasClut) {
		BlockHeader h = g.clutHeader;
		int declared = int(h.size) - 12;
		QByteArray clut = g.clut;
		if (clut.size() < declared) clut.append(QByteArray(declared - clut.size(), '\0'));
		else if (clut.size() > declared) clut.truncate(declared);
		appendHeader(out, h);
		out.append(clut);
	}
	for (const Image &im : g.images) {
		BlockHeader h = im.header;
		int declared = int(h.size) - 12;
		QByteArray d = im.data;
		if (d.size() < declared) d.append(QByteArray(declared - d.size(), '\0'));
		else if (d.size() > declared) d.truncate(declared);
		appendHeader(out, h);
		out.append(d);
	}
	out.append(g.extra);
	return out;
}

QByteArray rebuildAll(const QVector<Group> &groups) {
	QByteArray out;
	for (int i = 0; i < groups.size(); ++i) {
		if (i == 0 && !groups[i].prefix.isEmpty())
			out.append(groups[i].prefix);
		out.append(rebuildGroup(groups[i]));
	}
	return out;
}

bool parseGroupRaw(const QByteArray &data, int offset, int *newOffset, Group *out) {
	return parseGroup(data, offset, newOffset, /*checkSignature*/ false, out);
}

static BlockHeader headerFromJson(const QJsonObject &o) {
	BlockHeader h;
	h.size	   = quint32(o["size"].toString().toULongLong());
	h.x		  = quint16(o["x"].toInt());
	h.y		  = quint16(o["y"].toInt());
	h.widthWords = quint16(o["widthWords"].toInt());
	h.height	 = quint16(o["height"].toInt());
	return h;
}

QJsonObject groupToJson(const Group &g) {
	QJsonObject o;
	o["flags"]	   = QString::number(g.flags);
	o["bpp"]		 = int(g.bpp);
	o["hasClut"]	 = g.hasClut;
	o["hasImage"]	= g.hasImage;
	o["startOffset"] = g.startOffset;

	QJsonObject ch;
	ch["size"]	   = QString::number(g.clutHeader.size);
	ch["x"]		  = int(g.clutHeader.x);
	ch["y"]		  = int(g.clutHeader.y);
	ch["widthWords"] = int(g.clutHeader.widthWords);
	ch["height"]	 = int(g.clutHeader.height);
	o["clutHeader"]  = ch;

	o["clut"] = QString::fromLatin1(g.clut.toBase64());

	QJsonArray imgs;
	for (const Image &im : g.images) {
		QJsonObject io;
		QJsonObject ih;
		ih["size"]	   = QString::number(im.header.size);
		ih["x"]		  = int(im.header.x);
		ih["y"]		  = int(im.header.y);
		ih["widthWords"] = int(im.header.widthWords);
		ih["height"]	 = int(im.header.height);
		io["header"]	 = ih;
		imgs.append(io);
	}
	o["images"] = imgs;
	o["extra"]  = QString::fromLatin1(g.extra.toBase64());
	o["prefix"] = QString::fromLatin1(g.prefix.toBase64());
	
	QJsonArray warns;
	for (const QString &w : g.warnings) warns.append(w);
	o["warnings"] = warns;
	return o;
}

Group groupFromJson(const QJsonObject &o) {
	Group g;
	g.flags	   = quint32(o["flags"].toString().toULongLong());
	g.bpp		 = quint8(o["bpp"].toInt());
	g.hasClut	 = o["hasClut"].toBool();
	g.hasImage	= o["hasImage"].toBool();
	g.startOffset = o["startOffset"].toInt();
	if (o.contains("clutHeader"))
		g.clutHeader = headerFromJson(o["clutHeader"].toObject());
	g.clut = QByteArray::fromBase64(o["clut"].toString().toLatin1());
	for (const QJsonValue &v : o["images"].toArray()) {
		QJsonObject io = v.toObject();
		Image im;
		im.header = headerFromJson(io["header"].toObject());
		if (io.contains("data"))
			im.data = QByteArray::fromBase64(io["data"].toString().toLatin1());
		g.images.append(im);
	}
	g.extra = QByteArray::fromBase64(o["extra"].toString().toLatin1());
	g.prefix = QByteArray::fromBase64(o["prefix"].toString().toLatin1());
	for (const QJsonValue &v : o["warnings"].toArray())
		g.warnings << v.toString();
	return g;
}

// Валидация .ttptt перед открытием (не настроена и может отрицательно сказаться
// на процессе, так как нет достаточной практики при работы с tim)
/* Group groupFromJson(const QJsonObject &o) {
    Group g;
    g.flags = quint32(o["flags"].toString().toULongLong());
    g.bpp   = quint8(o["bpp"].toInt());

    // ── защита от мусора ───────────────────────────────────────
    if (g.bpp > 3) g.bpp = 0;
    g.flags = (g.flags & ~0x7u) | g.bpp;   // согласовать bpp/flags

    g.hasClut  = o["hasClut"].toBool();
    g.hasImage = o["hasImage"].toBool();
    g.startOffset = o["startOffset"].toInt();

    if (o.contains("clutHeader"))
        g.clutHeader = headerFromJson(o["clutHeader"].toObject());

    if (g.clutHeader.size < 12) g.clutHeader.size = 12;
    if (g.clutHeader.widthWords > 1024) g.clutHeader.widthWords = 1024;
    if (g.clutHeader.height > 1024)     g.clutHeader.height = 1024;

    g.clut = QByteArray::fromBase64(o["clut"].toString().toLatin1());
    int maxClutData = int(g.clutHeader.widthWords) * int(g.clutHeader.height) * 2;
    if (maxClutData > 4 * 1024 * 1024) maxClutData = 4 * 1024 * 1024;
    if (g.clut.size() > maxClutData) g.clut = g.clut.left(maxClutData);
    if (g.hasClut && int(g.clutHeader.size) - 12 < g.clut.size())
        g.clutHeader.size = 12 + g.clut.size();

    for (const QJsonValue &v : o["images"].toArray()) {
        if (g.images.size() >= 100) break;   // лимит на группу
        QJsonObject io = v.toObject();
        Image im;
        im.header = headerFromJson(io["header"].toObject());

        if (im.header.size < 12) im.header.size = 12;
        if (im.header.widthWords > 2048) im.header.widthWords = 2048;
        if (im.header.height > 2048)     im.header.height = 2048;
        if (im.header.widthWords == 0 || im.header.height == 0) continue;

        // отсекаем абсурдно большие изображения
        int pxW = widthWordsToPixels(g.bpp, im.header.widthWords);
        int pxH = im.header.height;
        if (qint64(pxW) * pxH > 16 * 1024 * 1024) continue;

        int maxData = int(im.header.widthWords) * int(im.header.height) * 2;
        if (maxData > 16 * 1024 * 1024) maxData = 16 * 1024 * 1024;

        if (io.contains("data"))
            im.data = QByteArray::fromBase64(io["data"].toString().toLatin1());
        if (im.data.size() > maxData) im.data = im.data.left(maxData);
        if (int(im.header.size) - 12 < im.data.size())
            im.header.size = 12 + im.data.size();

        g.images.append(im);
    }

    g.extra  = QByteArray::fromBase64(o["extra"].toString().toLatin1());
    g.prefix = QByteArray::fromBase64(o["prefix"].toString().toLatin1());
    for (const QJsonValue &v : o["warnings"].toArray())
        g.warnings << v.toString();
    return g;
}
 */
} // namespace Tim