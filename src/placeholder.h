// ttptt — TIM <-> Picture converter for PlayStation 1
// File: placeholder.h
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

namespace Placeholder {

void drawPSButton(QPainter &p, const QRectF &r, int type, const QColor &c);

QImage renderPlaceholder(const QSize &size,
                         const QFont &baseFont,
                         const QString &title,
                         const QString &subtitle);

QImage makeMissingImage(int w, int h, const QString &imgName = {},
                        const QVector<QColor> &palette = {});

} // namespace Placeholder