// ttptt — TIM <-> Picture converter for PlayStation 1
// File: timhelpers.h
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QVector>
#include "timformat.h"

#define TTPTT_PROJECT_VERSION 1

QMessageBox::StandardButton showMsgBox(
    QWidget *parent, QMessageBox::Icon icon,
    const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultBtn = QMessageBox::NoButton);

bool parseFlexible(const QString &sIn, int *out, QString *err = nullptr);

bool imageHasStpPixels(const Tim::Image &im, int bpp);
bool groupHasAnyStp(const Tim::Group &g);
bool needsMultiPalSuffix(const Tim::Group &g, bool enabled);

bool    writeTtpttFile(const QString &path, const QVector<Tim::Group> &groups,
                       const QString &sourcePath = QString(),
                       int paletteRow = 0, int currentEntry = 0,
                       bool showStp = false);

QString findTimImage(const QString &dirPath, const QString &stem,
                     int globalIdx, bool singleImage, int paletteRow);

QStringList autoSavePalettesHelper(const Tim::Group &g, const QString &baseNoext,
                                   bool act, bool pal, bool gpl);