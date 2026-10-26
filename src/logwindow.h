// ttptt — TIM <-> PNG converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QMutex>

class LogWindow : public QWidget {
	Q_OBJECT
public:
	explicit LogWindow(QWidget *parent = nullptr);
	~LogWindow() override;
	static void installMessageHandler();
	static LogWindow *instance();

public slots:
	void clearLog();
	void copyAll();
	void onNewMessage(const QString &msg) { emit newMessage(msg); }

signals:
	void newMessage(const QString &msg);
	void closed();

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	QPlainTextEdit *m_text = nullptr;
};