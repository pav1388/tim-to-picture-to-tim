// ttptt — TIM <-> Picture converter for PlayStation 1
// File: fileassoc.h
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QString>

class QWidget;

namespace FileAssoc {

void registerFileAssociation(const QString &ext, QWidget *parent);
void unregisterFileAssociation(const QString &ext, QWidget *parent);

} // namespace FileAssoc