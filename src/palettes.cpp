// ttptt — TIM <-> Picture converter for PlayStation 1
// File: palettes.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "palettes.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>
#include <QHash>

namespace Palettes {

using PaletteParser = QVector<QColor> (*)(const QByteArray &, bool *ok);

// =====================================================================
// Common skeleton: "read file + parse"
// =====================================================================
static QVector<QColor> readPaletteFile(const QString &path,
									   PaletteParser parser, bool *ok)
{
	*ok = false;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return {};
	return parser(f.readAll(), ok);
}

// =====================================================================
// ACT
// =====================================================================
static QVector<QColor> parseAct(const QByteArray &data, bool *ok) {
	*ok = false;
	int n = 0;
	if (data.size() == 772) {
		const uchar *p = reinterpret_cast<const uchar*>(data.constData() + 768);
		quint16 raw = (quint16(p[0]) << 8) | quint16(p[1]);
		n = (raw == 0) ? 256 : qMin<int>(raw, 256);
	} else if (data.size() == 768) {
		n = 256;
	} else if (data.size() >= 3 && data.size() % 3 == 0 && data.size() / 3 <= 256) {
		n = data.size() / 3;
	} else {
		return {};
	}
	QVector<QColor> out;
	out.reserve(n);
	for (int i = 0; i < n; ++i)
		out.append(QColor(uchar(data[i*3]), uchar(data[i*3+1]), uchar(data[i*3+2])));
	*ok = true;
	return out;
}

static void writeActFile(const QString &path, const QVector<QColor> &pal) {
	QByteArray out(772, '\0');
	for (int i = 0; i < 256; ++i) {
		if (i < pal.size()) {
			out[i*3]   = char(pal[i].red());
			out[i*3+1] = char(pal[i].green());
			out[i*3+2] = char(pal[i].blue());
		}
	}
	int count = pal.size() < 256 ? pal.size() : 0;
	out[768] = char((count >> 8) & 0xFF);
	out[769] = char(count & 0xFF);
	out[770] = char(0xFF); out[771] = char(0xFF);
	QFile f(path); f.open(QIODevice::WriteOnly); f.write(out);
}

// =====================================================================
// JASC-PAL
// =====================================================================
static QVector<QColor> parseJascPal(const QByteArray &data, bool *ok) {
	*ok = false;
	const QList<QByteArray> lines = data.split('\n');
	if (lines.size() < 3) return {};
	if (!QString::fromLatin1(lines[0]).trimmed().toUpper().startsWith("JASC-PAL"))
		return {};
	const int count = QString::fromLatin1(lines[2]).trimmed().toInt();

	auto splitWs = [](const QByteArray &line) -> QVector<QByteArray> {
		QVector<QByteArray> parts;
		QByteArray cur;
		for (char c : line) {
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
				if (!cur.isEmpty()) { parts.append(cur); cur.clear(); }
			} else {
				cur.append(c);
			}
		}
		if (!cur.isEmpty()) parts.append(cur);
		return parts;
	};

	QVector<QColor> out;
	for (int i = 3; i < 3 + count && i < lines.size(); ++i) {
		const QVector<QByteArray> parts = splitWs(lines[i]);
		if (parts.size() < 3) continue;
		bool o1, o2, o3;
		int r = parts[0].toInt(&o1);
		int g = parts[1].toInt(&o2);
		int b = parts[2].toInt(&o3);
		if (o1 && o2 && o3) out.append(QColor(r, g, b));
	}
	*ok = true;
	return out;
}

static void writeJascPal(const QString &path, const QVector<QColor> &pal) {
	QFile f(path); f.open(QIODevice::WriteOnly | QIODevice::Text);
	QTextStream s(&f);
	s << "JASC-PAL\n0100\n" << pal.size() << "\n";
	for (auto &c : pal) s << c.red() << " " << c.green() << " " << c.blue() << "\n";
}

// =====================================================================
// GIMP GPL
// =====================================================================
static QVector<QColor> parseGpl(const QByteArray &data, bool *ok) {
	*ok = false;
	const QList<QByteArray> lines = data.split('\n');
	if (lines.isEmpty()) return {};
	if (!QString::fromLatin1(lines[0]).trimmed().toUpper().startsWith("GIMP PALETTE"))
		return {};
	QVector<QColor> out;
	for (int i = 1; i < lines.size(); ++i) {
		const QString s = QString::fromLatin1(lines[i]).trimmed();
		if (s.isEmpty() || s.startsWith('#') ||
			s.startsWith("Name:") || s.startsWith("Columns:")) continue;
		const QStringList parts = s.split(QRegExp("\\s+"), QString::SkipEmptyParts);
		if (parts.size() < 3) continue;
		bool o1, o2, o3;
		int r = parts[0].toInt(&o1), g = parts[1].toInt(&o2), b = parts[2].toInt(&o3);
		if (o1 && o2 && o3) out.append(QColor(r, g, b));
	}
	*ok = true;
	return out;
}

static void writeGpl(const QString &path, const QVector<QColor> &pal) {
	QFile f(path); f.open(QIODevice::WriteOnly | QIODevice::Text);
	QTextStream s(&f);
	// "GIMP Palette", "Name:", "Columns:", "#" are format markers — DO NOT translate.
	s << "GIMP Palette\n";
	s << "Name: " << QCoreApplication::translate("Palettes", "ttptt export") << "\n";
	s << "Columns: 16\n#\n";
	const QString colorLabel = QCoreApplication::translate("Palettes", "Untitled");
	for (auto &c : pal)
		s << QString("%1 %2 %3\t%4\n")
			 .arg(c.red(), 3).arg(c.green(), 3).arg(c.blue(), 3)
			 .arg(colorLabel);
}

// =====================================================================
// PNG / BMP (palette as an image)
// =====================================================================
static Loaded readImage(const QString &path) {
	Loaded r;
	QImage img(path);
	if (img.isNull()) return r;
	img = img.convertToFormat(QImage::Format_RGB32);
	r.width = img.width();
	r.height = img.height();
	for (int y = 0; y < img.height(); ++y)
		for (int x = 0; x < img.width(); ++x) {
			QRgb c = img.pixel(x, y);
			r.colors.append(QColor(qRed(c), qGreen(c), qBlue(c)));
		}
	r.valid = true;
	return r;
}

static void writePng(const QString &path, const QVector<QColor> &pal, int w, int h) {
	if (w <= 0 || h <= 0) {
		int n = qMax(1, pal.size());
		w = n; h = 1;
	}
	QImage img(w, h, QImage::Format_RGB32);
	for (int i = 0; i < w * h; ++i) {
		QColor c = (i < pal.size()) ? pal[i] : QColor(0, 0, 0);
		img.setPixel(i % w, i / w, qRgb(c.red(), c.green(), c.blue()));
	}
	img.save(path);
}

// =====================================================================
// Public API
// =====================================================================
Loaded loadAny(const QString &path, QString *error) {
	const QString ext = QFileInfo(path).suffix().toLower();
	Loaded r;
	if (ext == "png" || ext == "bmp") {
		r = readImage(path);
		if (!r.valid && error) *error = QCoreApplication::translate("Palettes", "Failed to read image");
		return r;
	}

	PaletteParser parser = nullptr;
	if	  (ext == "act") parser = parseAct;
	else if (ext == "pal") parser = parseJascPal;
	else if (ext == "gpl") parser = parseGpl;
	else { if (error) *error = QCoreApplication::translate("Palettes", "Unsupported format: %1").arg(ext); return r; }

	bool ok = false;
	r.colors = readPaletteFile(path, parser, &ok);
	if (!ok) { if (error) *error = QCoreApplication::translate("Palettes", "Failed to read palette"); return r; }
	r.valid = true;
	return r;
}

bool saveAny(const QString &path, const QVector<QColor> &pal,
			 int width, int height, QString *error) {
	QString ext = QFileInfo(path).suffix().toLower();
	if (ext == "act") { writeActFile(path, pal); return true; }
	if (ext == "pal") { writeJascPal(path, pal); return true; }
	if (ext == "gpl") { writeGpl(path, pal); return true; }
	if (ext == "png" || ext == "bmp") { writePng(path, pal, width, height); return true; }
	if (error) *error = QCoreApplication::translate("Palettes", "Unsupported format: %1").arg(ext);
	return false;
}

} // namespace Palettes