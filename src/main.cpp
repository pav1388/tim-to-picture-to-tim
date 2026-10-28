// ttptt — TIM <-> Picture converter for PlayStation 1
// File: main.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QtPlugin>
#include <QStyleFactory>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>
#include <QLibraryInfo>
#include <QDir>

#include "mainwindow.h"
#include "logwindow.h"

#ifndef APP_VERSION
#define APP_VERSION "er-dev"
#endif

Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

static QString settingsFilePath()
{
	QString iniPath = QDir(QCoreApplication::applicationDirPath())
		.filePath(QStringLiteral("ttptt-settings.ini"));
	QFileInfo fi(iniPath);
	const bool writable = fi.exists() ? fi.isWritable()
		: QFileInfo(QCoreApplication::applicationDirPath()).isWritable();
	if (!writable) {
		const QString fallbackDir =
			QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
		QDir().mkpath(fallbackDir);
		iniPath = fallbackDir + "/ttptt-settings.ini";
	}
	return iniPath;
}

static void installTranslators(QApplication &app)
{
	QSettings settings(settingsFilePath(), QSettings::IniFormat);

	const QString language = settings.value(QStringLiteral("language"),
											QStringLiteral("en")).toString();
	if (language.isEmpty() || language == QLatin1String("en"))
		return;

	const QString appTrDir = QDir(QCoreApplication::applicationDirPath())
								 .filePath(QStringLiteral("translations"));

	QTranslator *qtTranslator = new QTranslator(&app);
	if (qtTranslator->load(QStringLiteral("qtbase_") + language, appTrDir)) {
		app.installTranslator(qtTranslator);
	} else if (qtTranslator->load(QStringLiteral("qtbase_") + language,
								  QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
		app.installTranslator(qtTranslator);
	} else if (qtTranslator->load(QStringLiteral(":/translations/qtbase_") + language)) {
		app.installTranslator(qtTranslator);
	} else {
		delete qtTranslator;
	}

	QTranslator *appTranslator = new QTranslator(&app);
	if (appTranslator->load(QStringLiteral("ttptt_") + language, appTrDir)) {
		app.installTranslator(appTranslator);
	} else if (appTranslator->load(QStringLiteral(":/translations/ttptt_") + language)) {
		app.installTranslator(appTranslator);
	} else {
		delete appTranslator;
	}
}

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	QApplication::setStyle(QStyleFactory::create("Fusion"));
	
	QFont appFont = app.font();
	appFont.setPointSize(9);
	app.setFont(appFont);
	
	app.setApplicationName("ttptt");
	app.setApplicationVersion(APP_VERSION);
	app.setOrganizationName("pav13");

	installTranslators(app);

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