// ttptt — TIM <-> Picture converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QColor>
#include <QVector>
#include <QString>

namespace Palettes {

struct Loaded {
	QVector<QColor> colors;
	int width = 0, height = 0;
	bool valid = false;
};

Loaded loadAny(const QString &path, QString *error = nullptr);
bool saveAny(const QString &path, const QVector<QColor> &pal,
			 int width, int height, QString *error = nullptr);

} // namespace Palettes