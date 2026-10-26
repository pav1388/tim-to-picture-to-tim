// ttptt — TIM <-> Picture converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QtPlugin>
#include <QStyleFactory>
#include <QFileInfo>

#include "mainwindow.h"
#include "logwindow.h"

#ifndef APP_VERSION
#define APP_VERSION "-dev"
#endif

Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	QApplication::setStyle(QStyleFactory::create("Fusion"));
	
	QFont appFont = app.font();
	appFont.setPointSize(9);
	app.setFont(appFont);
	
	app.setApplicationName("ttptt");
	app.setApplicationVersion(APP_VERSION);
	app.setOrganizationName("pav13");

	LogWindow::installMessageHandler();

	MainWindow w;
	w.show();

	const QStringList args = QCoreApplication::arguments();
	for (int i = 1; i < args.size(); ++i) {
		if (QFileInfo::exists(args[i])) {
			QMetaObject::invokeMethod(&w, "openPathFromCommandLine",
									  Qt::QueuedConnection,
									  Q_ARG(QString, args[i]));
			break;
		}
	}

	return app.exec();
}