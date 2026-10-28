// ttptt — TIM <-> Picture converter for PlayStation 1
// File: timhelpers.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timhelpers.h"
#include "timcodec.h"
#include "palettes.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>

QMessageBox::StandardButton showMsgBox(
    QWidget *parent, QMessageBox::Icon icon,
    const QString &title, const QString &text,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultBtn)
{
    QMessageBox box(parent);
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    if (defaultBtn != QMessageBox::NoButton) box.setDefaultButton(defaultBtn);

    struct BtnLabel { QMessageBox::StandardButton b; const char *t; };
    static const BtnLabel kLabels[] = {
        { QMessageBox::Ok,      QT_TRANSLATE_NOOP("timhelpers", "OK") },
        { QMessageBox::Yes,     QT_TRANSLATE_NOOP("timhelpers", "Yes") },
        { QMessageBox::No,      QT_TRANSLATE_NOOP("timhelpers", "No") },
        { QMessageBox::Save,    QT_TRANSLATE_NOOP("timhelpers", "Save") },
        { QMessageBox::Discard, QT_TRANSLATE_NOOP("timhelpers", "Don't Save") },
        { QMessageBox::Cancel,  QT_TRANSLATE_NOOP("timhelpers", "Cancel") },
        { QMessageBox::Close,   QT_TRANSLATE_NOOP("timhelpers", "Close") },
        { QMessageBox::Abort,   QT_TRANSLATE_NOOP("timhelpers", "Abort") },
        { QMessageBox::Retry,   QT_TRANSLATE_NOOP("timhelpers", "Retry") },
        { QMessageBox::Ignore,  QT_TRANSLATE_NOOP("timhelpers", "Ignore") },
    };
    for (const auto &l : kLabels)
        if (auto *b = box.button(l.b))
            b->setText(QCoreApplication::translate("timhelpers", l.t));

    return static_cast<QMessageBox::StandardButton>(box.exec());
}

bool parseFlexible(const QString &sIn, int *out, QString *err) {
    QString s = sIn.trimmed();
    if (s.isEmpty()) {
        if (err) *err = QCoreApplication::translate("timhelpers", "Empty value");
        return false;
    }
    bool ok = false;
    int v = s.toInt(&ok, 0);
    if (!ok) {
        if (err)
            *err = QCoreApplication::translate("timhelpers", "Not a number: %1").arg(sIn);
        return false;
    }
    *out = v;
    return true;
}

bool imageHasStpPixels(const Tim::Image &im, int bpp) {
    if (bpp != 2) return false;
    const QByteArray &d = im.data;
    const uchar *p = reinterpret_cast<const uchar*>(d.constData());
    int n = d.size() / 2;
    for (int i = 0; i < n; ++i)
        if (Tim::rd16(p + i * 2) & 0x8000) return true;
    return false;
}

bool groupHasAnyStp(const Tim::Group &g) {
    if (g.bpp != 2) return false;
    for (const auto &im : g.images)
        if (imageHasStpPixels(im, g.bpp)) return true;
    return false;
}

bool needsMultiPalSuffix(const Tim::Group &g, bool enabled) {
    if (!enabled) return false;
    if (g.bpp != 0 && g.bpp != 1) return false;
    if (!g.hasClut || g.clutHeader.height <= 1) return false;
    return true;
}

bool writeTtpttFile(const QString &path, const QVector<Tim::Group> &groups,
                    const QString &sourcePath, int paletteRow,
                    int currentEntry, bool showStp)
{
    if (groups.isEmpty()) return false;

    QJsonObject root;
    root["version"]      = TTPTT_PROJECT_VERSION;
    root["app"]          = "ttptt";
    if (!sourcePath.isEmpty())
        root["sourcePath"] = sourcePath;
    root["paletteRow"]   = paletteRow;
    root["currentEntry"] = currentEntry;
    root["showStp"]      = showStp;

    QJsonArray garr;
    for (const Tim::Group &g : groups)
        garr.append(Tim::groupToJson(g));
    root["groups"] = garr;

    QJsonDocument doc(root);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QString findTimImage(const QString &dirPath, const QString &stem,
                     int globalIdx, bool singleImage, int paletteRow)
{
    QDir dir(dirPath);
    QStringList candidates;

    auto addPalCandidates = [&](const QString &base) {
        if (paletteRow < 0) return;
        candidates << QString("%1_pal%2.png").arg(base)
                          .arg(paletteRow + 1, 2, 10, QChar('0'));
        candidates << QString("%1_pal%2.bmp").arg(base)
                          .arg(paletteRow + 1, 2, 10, QChar('0'));
        candidates << QString("%1_pal%2.png").arg(base).arg(paletteRow + 1);
        candidates << QString("%1_pal%2.bmp").arg(base).arg(paletteRow + 1);
    };

    if (singleImage) {
        addPalCandidates(stem);
        candidates << (stem + ".png");
        candidates << (stem + ".bmp");
    }

    addPalCandidates(QString("%1_img%2").arg(stem).arg(globalIdx, 2, 10, QChar('0')));
    addPalCandidates(QString("%1_img%2").arg(stem).arg(globalIdx));
    candidates << QString("%1_img%2.png").arg(stem).arg(globalIdx, 2, 10, QChar('0'));
    candidates << QString("%1_img%2.bmp").arg(stem).arg(globalIdx, 2, 10, QChar('0'));
    candidates << QString("%1_img%2.png").arg(stem).arg(globalIdx);
    candidates << QString("%1_img%2.bmp").arg(stem).arg(globalIdx);

    for (const QString &cand : candidates) {
        const QString full = dir.filePath(cand);
        if (QFileInfo::exists(full)) return full;
    }
    return QString();
}

QStringList autoSavePalettesHelper(const Tim::Group &g, const QString &baseNoext,
                                   bool act, bool pal, bool gpl)
{
    QStringList saved;
    int cw = 0, ch = 0;
    QVector<QColor> flat = Tim::safeClutFlat(g, &cw, &ch);
    if (flat.isEmpty()) return saved;
    QString err;
    if (act && Palettes::saveAny(baseNoext + ".act", flat, cw, ch, &err)) saved << ".act";
    if (pal && Palettes::saveAny(baseNoext + ".pal", flat, cw, ch, &err)) saved << ".pal";
    if (gpl && Palettes::saveAny(baseNoext + ".gpl", flat, cw, ch, &err)) saved << ".gpl";
    return saved;
}