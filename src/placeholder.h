// ttptt — TIM <-> PNG converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QColor>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

class QPainter;

namespace PlaceholderArt {

void drawPSButton(QPainter &p, const QRectF &r, int type, const QColor &c);

QImage renderPlaceholder(const QSize &size,
						 const QFont &baseFont,
						 const QString &title,
						 const QString &subtitle,
						 int themeIndex = -1,
						 QString *themeNameOut = nullptr);
QImage makeMissingTimPng(int w, int h, int globalIdx, int bpp,
						 const QString &pngName = {});
int	 themeCount();
QString themeName(int index);

} // namespace PlaceholderArt