// ttptt — TIM <-> PNG converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "themes.h"
#include <QApplication>
#include <QStyle>
#include <QPalette>
#include <QColor>

void applyAppTheme(bool dark, QApplication *app) {
	if (!app) app = qApp;
	if (!app) return;

	if (dark) {
		QPalette p;
		p.setColor(QPalette::Window,		  QColor(43, 43, 43));
		p.setColor(QPalette::WindowText,	  Qt::white);
		p.setColor(QPalette::Base,			QColor(30, 30, 30));
		p.setColor(QPalette::AlternateBase,   QColor(43, 43, 43));
		p.setColor(QPalette::ToolTipBase,	 Qt::white);
		p.setColor(QPalette::ToolTipText,	 Qt::white);
		p.setColor(QPalette::Text,			Qt::white);
		p.setColor(QPalette::Button,		  QColor(58, 58, 58));
		p.setColor(QPalette::ButtonText,	  Qt::white);
		p.setColor(QPalette::BrightText,	  Qt::red);
		p.setColor(QPalette::Link,			QColor(42, 130, 218));
		p.setColor(QPalette::Highlight,	   QColor(42, 130, 218));
		p.setColor(QPalette::HighlightedText, Qt::black);
		app->setPalette(p);

		static const char kQss[] = R"(
			QComboBox, QLineEdit, QSpinBox, QCheckBox, QGroupBox, QToolTip, QPlainTextEdit, QTextEdit {
				background-color: #2b2b2b;
				color: #dcdcdc;
				border: 1px solid #555;
				border-radius: 3px;
				padding: 2px;
			}
			QComboBox::drop-down { border: 0px; }
			QComboBox QAbstractItemView {
				background-color: #2b2b2b;
				color: #dcdcdc;
				selection-background-color: #3c7fb1;
			}
			QPushButton {
				background-color: #3a3a3a;
				color: #dcdcdc;
				border: 1px solid #555;
				border-radius: 3px;
				padding: 4px 8px;
			}
			QPushButton:hover { background-color: #4a4a4a; }
			QPushButton:pressed { background-color: #2a2a2a; }
			QPushButton:checked {
				background-color: #3c7fb1;
				color: #ffffff;
				border: 1px solid #5aa0d8;
			}
			QPushButton:checked:hover {
				background-color: #4a90c5;
			}
			QGroupBox {
				border: 1px solid #555;
				margin-top: 8px;
				padding-top: 8px;
			}
			QGroupBox::title {
				subcontrol-origin: margin;
				left: 10px;
				padding: 0 3px 0 3px;
				color: #dcdcdc;
			}
			QMenuBar { background-color: #2b2b2b; color: #dcdcdc; }
			QMenuBar::item:selected { background-color: #3c7fb1; }
			QMenu { background-color: #2b2b2b; color: #dcdcdc; border: 1px solid #555; }
			QMenu::item:selected { background-color: #3c7fb1; }
			QStatusBar { background-color: #2b2b2b; color: #dcdcdc; }
		)";
		app->setStyleSheet(QString::fromLatin1(kQss));
	} else {
		app->setStyleSheet(QString());
		app->setPalette(app->style()->standardPalette());
	}
}