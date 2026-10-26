// ttptt — TIM <-> PNG converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
class QApplication;

// Применяет палитру + QSS ко всему приложению.
// app == nullptr → берётся qApp.
void applyAppTheme(bool dark, QApplication *app = nullptr);