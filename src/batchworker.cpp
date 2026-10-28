// ttptt — TIM <-> Picture converter for PlayStation 1
// File: batchworker.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "batchworker.h"
#include "timcodec.h"
#include "timhelpers.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

bool showBatchParamsDialog(QWidget *parent, BatchParams &params) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Параметры пакетного извлечения"));
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(&dlg);

    auto *gImg = new QGroupBox(QObject::tr("Изображения"), &dlg);
    auto *il   = new QVBoxLayout(gImg);
    auto *fh   = new QHBoxLayout();
    fh->addWidget(new QLabel(QObject::tr("Формат:"), gImg));
    auto *cbFormat = new QComboBox(gImg);
    cbFormat->addItem("PNG (*.png)", "png");
    cbFormat->addItem("BMP (*.bmp)", "bmp");
    cbFormat->setCurrentIndex(params.imageFormat == "bmp" ? 1 : 0);
    fh->addWidget(cbFormat, 1);
    il->addLayout(fh);
    auto *cbAllPal = new QCheckBox(
        QObject::tr("Сохранять все изображения в каждой палитре"), gImg);
    cbAllPal->setChecked(params.saveAllPalettes);
    il->addWidget(cbAllPal);
    layout->addWidget(gImg);

    auto *gPal = new QGroupBox(QObject::tr("Палитры рядом с изображением"), &dlg);
    auto *pl   = new QVBoxLayout(gPal);
    auto *cbAct = new QCheckBox(QObject::tr("ACT - Adobe Color Table (.act)"), gPal);
    auto *cbPal = new QCheckBox(QObject::tr("PAL - JASC Palette (.pal)"), gPal);
    auto *cbGpl = new QCheckBox(QObject::tr("GPL - GIMP Palette (.gpl)"), gPal);
    cbAct->setChecked(params.saveAct);
    cbPal->setChecked(params.savePal);
    cbGpl->setChecked(params.saveGpl);
    pl->addWidget(cbAct); pl->addWidget(cbPal); pl->addWidget(cbGpl);
    layout->addWidget(gPal);

    auto *gMeta = new QGroupBox(QObject::tr("Метаданные"), &dlg);
    auto *ml    = new QVBoxLayout(gMeta);
    auto *cbTtptt = new QCheckBox(
        QObject::tr("Сохранять .ttptt рядом с изображением"), gMeta);
    cbTtptt->setChecked(params.saveTtptt);
    ml->addWidget(cbTtptt);
    layout->addWidget(gMeta);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto *b = bb->button(QDialogButtonBox::Ok))     b->setText(QObject::tr("OK"));
    if (auto *b = bb->button(QDialogButtonBox::Cancel)) b->setText(QObject::tr("Отмена"));
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return false;

    params.imageFormat     = cbFormat->currentData().toString();
    params.saveAllPalettes = cbAllPal->isChecked();
    params.saveAct         = cbAct->isChecked();
    params.savePal         = cbPal->isChecked();
    params.saveGpl         = cbGpl->isChecked();
    params.saveTtptt       = cbTtptt->isChecked();
    return true;
}

void BatchWorker::runExtract(const QString &folder, bool anyPal, bool autoAct,
                             bool autoPal, bool autoGpl, bool saveAllPalettes,
                             bool saveMeta, const QString &imageExt) {
	QDir dir(folder);
	QStringList files = dir.entryList(QDir::Files);
	files.sort();
	int count = 0, timFiles = 0, skipped = 0, metaSaved = 0;
	int stpAffected = 0;
	QStringList errors;
	QStringList metaErrors;
	const QString ext = imageExt.startsWith('.') ? imageExt : ("." + imageExt);
	const bool bmpOutput = (ext.compare(".bmp", Qt::CaseInsensitive) == 0);

	for (int idx = 0; idx < files.size(); ++idx) {
		const QString &fname = files[idx];
		emit progress(idx + 1, files.size(), fname);
		QFile f(dir.filePath(fname));
		if (!f.open(QIODevice::ReadOnly)) {
			errors << fname + ": cannot open";
			continue;
		}
		QByteArray data = f.readAll();
		f.close();

		if (data.indexOf(QByteArray("\x10\x00\x00\x00", 4)) < 0) {
			++skipped;
			continue;
		}
		QVector<Tim::Group> groups = Tim::parseAll(data);
		if (groups.isEmpty()) {
			++skipped;
			continue;
		}
		++timFiles;

		QString stem = QFileInfo(fname).completeBaseName();
		if (stem.isEmpty()) stem = fname;

		if (saveMeta) {
			QString metaPath = dir.filePath(stem + ".ttptt");
			if (writeTtpttFile(metaPath, groups, dir.filePath(fname), 0, 0, false))
				++metaSaved;
			else
				metaErrors << fname + ": метаданные не сохранены";
		}

		int n = 0;
		for (const Tim::Group &g : groups) {
			for (const QString &w : g.warnings) emit logMsg(fname + ": " + w);

			if (bmpOutput && groupHasAnyStp(g)) {
				++stpAffected;
				emit logMsg(fname + ": !!! ВНИМАНИЕ: BMP потеряет STP-биты в 16bpp-группе");
			}

			for (int i = 0; i < g.images.size(); ++i) {
				++n;
				const QString basePrefix =
					QString("%1_img%2").arg(stem).arg(n, 2, 10, QChar('0'));

				const bool multiPal = saveAllPalettes &&
									  (g.bpp == 0 || g.bpp == 1) &&
									  g.hasClut && g.clutHeader.height > 1;
				const int ch = qMax<int>(1, g.clutHeader.height);

				if (multiPal) {
					for (int r = 0; r < ch; ++r) {
						QImage pimg = Tim::decodeImage(g, i, r, -1);
						if (pimg.isNull()) {
							errors << QString("%1 pal#%2: null image")
										  .arg(basePrefix).arg(r + 1);
							continue;
						}
						const QString ppath = dir.filePath(
							QString("%1_pal%2%3").arg(basePrefix)
								.arg(r + 1, 2, 10, QChar('0')).arg(ext));
						if (!pimg.save(ppath)) {
							errors << ppath + ": save failed";
							continue;
						}
						++count;
					}
				} else {
					const QString imgPath = dir.filePath(basePrefix + ext);
					QImage img = Tim::decodeImage(g, i, 0, -1);
					if (img.isNull()) {
						errors << imgPath + ": null image";
						continue;
					}
					if (!img.save(imgPath)) {
						errors << imgPath + ": save failed";
						continue;
					}
					++count;
				}

				if (anyPal) {
					autoSavePalettesHelper(g, dir.filePath(basePrefix),
										   autoAct, autoPal, autoGpl);
				}
			}
		}
	}

	QString msg = QString("Экспортировано %1 файлов (%2) из %3 TIM-файлов "
						  "(пропущено не-TIM: %4).")
		.arg(count).arg(ext).arg(timFiles).arg(skipped);
	if (saveMeta)
		msg += QString(" Метаданных: %1.").arg(metaSaved);
	if (stpAffected > 0) {
		msg += QString("\n\nВНИМАНИЕ: в %1 '16bpp-группе(ах)' были STP-пиксели. "
					   "BMP не хранит альфа-канал — при обратном конвертировании "
					   "STP-биты будут сброшены. Используйте PNG."
					   "Подробнее в Журнал логов (меню Разное)")
			.arg(stpAffected);
	}
	if (!metaErrors.isEmpty())
		errors.append(metaErrors);
	emit finished(msg, errors, QStringList(), false);
}
void BatchWorker::runReplace(const QString &folder, const QString &currentSourcePath) {
	QDir dir(folder);
	QStringList files = dir.entryList(QDir::Files);
	files.sort();
	int replaced = 0, missing = 0, timFiles = 0, changedFiles = 0;
	int stpLossCount = 0, skipped = 0, normalized = 0;
	QStringList errors;
	QStringList sizeMismatches;
	bool openedFileRewritten = false;

	for (int idx = 0; idx < files.size(); ++idx) {
		const QString &fname = files[idx];
		emit progress(idx + 1, files.size(), fname);
		const QString path = dir.filePath(fname);
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly)) { errors << fname + ": cannot open"; continue; }
		QByteArray data = f.readAll();
		f.close();
		if (data.indexOf(QByteArray("\x10\x00\x00\x00", 4)) < 0) { ++skipped; continue; }
		QVector<Tim::Group> groups = Tim::parseAll(data);
		if (groups.isEmpty()) { ++skipped; continue; }
		++timFiles;

		QString stem = QFileInfo(fname).completeBaseName();
		if (stem.isEmpty()) stem = fname;

		int n = 0;
		bool changed = false;
		for (Tim::Group &g : groups) {
			for (int i = 0; i < g.images.size(); ++i) {
				++n;
				const int w = Tim::widthWordsToPixels(g.bpp, g.images[i].header.widthWords);
				const int h = g.images[i].header.height;

				const QString imagePath = findTimImage(dir.path(), stem, n, false, -1);
				if (imagePath.isEmpty()) { ++missing; continue; }
				const QString imgName = QFileInfo(imagePath).fileName();

				QImage im(imagePath);
				if (im.isNull()) {
					errors << imgName + " -> " + fname + ": open failed";
					continue;
				}
				const bool bmpLossy = imgName.endsWith(".bmp", Qt::CaseInsensitive) &&
									  g.bpp == 2 &&
									  imageHasStpPixels(g.images[i], g.bpp);
				if (bmpLossy) {
					emit logMsg(QString("%1 -> %2: BMP стирает STP-биты в 16bpp")
									.arg(imgName, fname));
					++stpLossCount;
				}
				if (im.width() != w || im.height() != h) {
					sizeMismatches << QString("%1 -> %2: image %3x%4, TIM %5x%6")
						.arg(imgName, fname)
						.arg(im.width()).arg(im.height()).arg(w).arg(h);
				}
				im = Tim::normalizeImageForTim(im, w, h);
				if (!Tim::applyEncodedToImage(g, i, im, 0)) {
					errors << imgName + " -> " + fname + ": encode failed";
					continue;
				}
				changed = true;
				++replaced;
			}
		}
		if (!changed) continue;

		QByteArray newBytes = Tim::rebuildAll(groups);
		if (newBytes.size() != data.size()) {
			++normalized;
			emit logMsg(QString("%1: !!! ВНИМАНИЕ: нормализован %2 -> %3")
				.arg(fname).arg(data.size()).arg(newBytes.size()));
		}
		QFile out(path);
		if (!out.open(QIODevice::WriteOnly)) {
			errors << "write " + fname + ": cannot open";
			continue;
		}
		out.write(newBytes);
		out.close();
		++changedFiles;
		if (!currentSourcePath.isEmpty() &&
			QFileInfo(path).absoluteFilePath() == QFileInfo(currentSourcePath).absoluteFilePath()) {
			openedFileRewritten = true;
		}
	}

	QString msg = QString("Изменено %1 TIM-файлов, заменено %2 изображений "
						  "(пропущено не-TIM: %3, не найдено изображений: %4, нормализовано: %5).")
		.arg(changedFiles).arg(replaced).arg(skipped).arg(missing).arg(normalized);
	if (stpLossCount > 0) {
		msg += QString("\n\nВНИМАНИЕ: в %1 файле(ах) BMP-замена стёрла STP-биты "
					   "в 16bpp-группах. Используйте PNG.").arg(stpLossCount);
	}
	emit finished(msg, errors, sizeMismatches, openedFileRewritten);
}