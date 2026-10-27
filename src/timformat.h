// ttptt — TIM <-> Picture converter for PlayStation 1
// File: timformat.h
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <QJsonObject>

namespace Tim {

struct BlockHeader {
	quint32 size = 0;
	quint16 x = 0, y = 0;
	quint16 widthWords = 0;
	quint16 height = 0;
};

struct Image {
	BlockHeader header;
	QByteArray data;
};

struct Group {
	quint32 flags = 0;
	quint8 bpp = 0;
	bool hasClut = false;
	bool hasImage = false;
	BlockHeader clutHeader;
	QByteArray clut;
	QVector<Image> images;
	QByteArray extra;
	QByteArray prefix;
	QStringList warnings;
	int startOffset = 0;
};

QVector<Group> parseAll(const QByteArray &data);
QByteArray rebuildGroup(const Group &g);
QByteArray rebuildAll(const QVector<Group> &groups);

int widthWordsToPixels(int bpp, int words);
int pixelsToWords(int bpp, int pixels);
bool parseGroupRaw(const QByteArray &data, int offset, int *newOffset, Group *out);

QJsonObject groupToJson(const Group &g);
Group	  groupFromJson(const QJsonObject &o);

inline quint16 rd16(const uchar *p) { return quint16(p[0]) | (quint16(p[1]) << 8); }
inline quint32 rd32(const uchar *p) {
	return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}
inline void wr16(uchar *p, quint16 v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
inline void wr32(uchar *p, quint32 v) {
	p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

} // namespace Tim