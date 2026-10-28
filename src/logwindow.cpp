// ttptt — TIM <-> Picture converter for PlayStation 1
// File: logwindow.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QMutexLocker>
#include <QCloseEvent>

static QMutex g_logMutex;
static LogWindow *g_instance = nullptr;

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
	const char *level = "INFO";
	switch (type) {
	case QtDebugMsg:	level = "DEBUG"; break;
	case QtInfoMsg:	 level = "INFO";  break;
	case QtWarningMsg:  level = "WARN";  break;
	case QtCriticalMsg: level = "ERROR"; break;
	case QtFatalMsg:	level = "FATAL"; break;
	}
	QString line = QString("[%1] %2").arg(level, msg);
	QMutexLocker lock(&g_logMutex);
	if (g_instance) {
		QMetaObject::invokeMethod(g_instance, "onNewMessage",
								   Qt::QueuedConnection,
								   Q_ARG(QString, line));
	}
}

LogWindow::LogWindow(QWidget *parent) : QWidget(parent, Qt::Window) {
	setWindowTitle(tr("Log Window"));
	resize(560, 400);

	auto *layout = new QVBoxLayout(this);
	m_text = new QPlainTextEdit(this);
	m_text->setReadOnly(true);
	QFont mono("Courier New", 9);
	m_text->setFont(mono);
	layout->addWidget(m_text);

	auto *btns = new QHBoxLayout();
	auto *btnClear = new QPushButton(tr("Clear"), this);
	auto *btnCopy = new QPushButton(tr("Copy"), this);
	btns->addWidget(btnClear);
	btns->addWidget(btnCopy);
	btns->addStretch();
	layout->addLayout(btns);

	connect(btnClear, &QPushButton::clicked, this, &LogWindow::clearLog);
	connect(btnCopy, &QPushButton::clicked, this, &LogWindow::copyAll);
	connect(this, &LogWindow::newMessage, this, [this](const QString &m) {
		m_text->appendPlainText(m);
	});

	g_instance = this;
}

LogWindow::~LogWindow() {
	QMutexLocker lock(&g_logMutex);
	if (g_instance == this) g_instance = nullptr;
}

void LogWindow::closeEvent(QCloseEvent *e) {
	emit closed();
	QWidget::closeEvent(e);
}

LogWindow *LogWindow::instance() { return g_instance; }

void LogWindow::installMessageHandler() {
	qInstallMessageHandler(messageHandler);
}

void LogWindow::clearLog() { m_text->clear(); }

void LogWindow::copyAll() {
	if (auto *app = qApp)
		app->clipboard()->setText(m_text->toPlainText());
}