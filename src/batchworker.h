// ttptt — TIM <-> Picture converter for PlayStation 1
// File: batchworker.h
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

class QWidget;

struct BatchParams {
    bool    saveAct         = false;
    bool    savePal         = false;
    bool    saveGpl         = false;
    bool    saveTtptt       = false;
    bool    saveAllPalettes = false;
    QString imageFormat     = "png";
};

bool showBatchParamsDialog(QWidget *parent, BatchParams &params);

class BatchWorker : public QObject {
    Q_OBJECT
public:
    explicit BatchWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void runExtract(const QString &folder, bool anyPal, bool autoAct, bool autoPal,
                    bool autoGpl, bool saveAllPalettes, bool saveMeta,
                    const QString &imageExt);
    void runReplace(const QString &folder, const QString &currentSourcePath);

signals:
    void progress(int current, int total, const QString &fileName);
    void logMsg(const QString &msg);
    void finished(const QString &summary, const QStringList &errors,
                  const QStringList &warnings, bool openedFileRewritten);
};