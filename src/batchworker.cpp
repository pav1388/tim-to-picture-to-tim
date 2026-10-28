// ttptt — TIM <-> Picture converter for PlayStation 1
// File: batchworker.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "batchworker.h"
#include "timcodec.h"
#include "timhelpers.h"

#include <QCoreApplication>
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
    dlg.setWindowTitle(QCoreApplication::translate("batchworker", "Batch Extract Options"));
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(&dlg);

    auto *gImg = new QGroupBox(QCoreApplication::translate("batchworker", "Images"), &dlg);
    auto *il   = new QVBoxLayout(gImg);
    auto *fh   = new QHBoxLayout();
    fh->addWidget(new QLabel(QCoreApplication::translate("batchworker", "Format:"), gImg));
    auto *cbFormat = new QComboBox(gImg);
    cbFormat->addItem("PNG (*.png)", "png");
    cbFormat->addItem("BMP (*.bmp)", "bmp");
    cbFormat->setCurrentIndex(params.imageFormat == "bmp" ? 1 : 0);
    fh->addWidget(cbFormat, 1);
    il->addLayout(fh);
    auto *cbAllPal = new QCheckBox(
        QCoreApplication::translate("batchworker", "Save all images in each palette"), gImg);
    cbAllPal->setChecked(params.saveAllPalettes);
    il->addWidget(cbAllPal);
    layout->addWidget(gImg);

    auto *gPal = new QGroupBox(QCoreApplication::translate("batchworker", "Palettes next to image"), &dlg);
    auto *pl   = new QVBoxLayout(gPal);
    auto *cbAct = new QCheckBox(QCoreApplication::translate("batchworker", "ACT - Adobe Color Table (.act)"), gPal);
    auto *cbPal = new QCheckBox(QCoreApplication::translate("batchworker", "PAL - JASC Palette (.pal)"), gPal);
    auto *cbGpl = new QCheckBox(QCoreApplication::translate("batchworker", "GPL - GIMP Palette (.gpl)"), gPal);
    cbAct->setChecked(params.saveAct);
    cbPal->setChecked(params.savePal);
    cbGpl->setChecked(params.saveGpl);
    pl->addWidget(cbAct); pl->addWidget(cbPal); pl->addWidget(cbGpl);
    layout->addWidget(gPal);

    auto *gMeta = new QGroupBox(QCoreApplication::translate("batchworker", "Metadata"), &dlg);
    auto *ml    = new QVBoxLayout(gMeta);
    auto *cbTtptt = new QCheckBox(
        QCoreApplication::translate("batchworker", "Save .ttptt next to image"), gMeta);
    cbTtptt->setChecked(params.saveTtptt);
    ml->addWidget(cbTtptt);
    layout->addWidget(gMeta);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto *b = bb->button(QDialogButtonBox::Ok))     b->setText(QCoreApplication::translate("batchworker", "OK"));
    if (auto *b = bb->button(QDialogButtonBox::Cancel)) b->setText(QCoreApplication::translate("batchworker", "Cancel"));
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
			errors << tr("%1: cannot open").arg(fname);
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
				metaErrors << tr("%1: metadata not saved").arg(fname);
		}

		int n = 0;
		for (const Tim::Group &g : groups) {
			for (const QString &w : g.warnings) emit logMsg(fname + ": " + w);

			if (bmpOutput && groupHasAnyStp(g)) {
				++stpAffected;
				emit logMsg(tr("%1: !!! WARNING: BMP will lose STP bits in 16bpp group")
								.arg(fname));
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
							errors << tr("%1 pal#%2: null image")
										  .arg(basePrefix).arg(r + 1);
							continue;
						}
						const QString ppath = dir.filePath(
							QString("%1_pal%2%3").arg(basePrefix)
								.arg(r + 1, 2, 10, QChar('0')).arg(ext));
						if (!pimg.save(ppath)) {
							errors << tr("%1: save failed").arg(ppath);
							continue;
						}
						++count;
					}
				} else {
					const QString imgPath = dir.filePath(basePrefix + ext);
					QImage img = Tim::decodeImage(g, i, 0, -1);
					if (img.isNull()) {
						errors << tr("%1: null image").arg(imgPath);
						continue;
					}
					if (!img.save(imgPath)) {
						errors << tr("%1: save failed").arg(imgPath);
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

	QString msg = tr("Exported %1 file(s) (%2) from %3 TIM file(s) "
					 "(non-TIM skipped: %4).")
		.arg(count).arg(ext).arg(timFiles).arg(skipped);
	if (saveMeta)
		msg += tr(" Metadata saved: %1.").arg(metaSaved);
	if (stpAffected > 0) {
		msg += tr("\n\nWARNING: %1 '16bpp group(s)' contained STP pixels. "
				  "BMP has no alpha channel — on reverse conversion the STP "
				  "bits will be reset. Use PNG instead. "
				  "See the Log Window for details (Settings menu).")
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
		if (!f.open(QIODevice::ReadOnly)) { errors << tr("%1: cannot open").arg(fname); continue; }
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
					errors << tr("%1 -> %2: open failed").arg(imgName, fname);
					continue;
				}
				const bool bmpLossy = imgName.endsWith(".bmp", Qt::CaseInsensitive) &&
									  g.bpp == 2 &&
									  imageHasStpPixels(g.images[i], g.bpp);
				if (bmpLossy) {
					emit logMsg(tr("%1 -> %2: BMP strips STP bits in 16bpp")
									.arg(imgName, fname));
					++stpLossCount;
				}
				if (im.width() != w || im.height() != h) {
					sizeMismatches << tr("%1 -> %2: image %3x%4, TIM %5x%6")
						.arg(imgName, fname)
						.arg(im.width()).arg(im.height()).arg(w).arg(h);
				}
				im = Tim::normalizeImageForTim(im, w, h);
				if (!Tim::applyEncodedToImage(g, i, im, 0)) {
					errors << tr("%1 -> %2: encode failed").arg(imgName, fname);
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
			emit logMsg(tr("%1: !!! WARNING: normalized %2 -> %3")
				.arg(fname).arg(data.size()).arg(newBytes.size()));
		}
		QFile out(path);
		if (!out.open(QIODevice::WriteOnly)) {
			errors << tr("write %1: cannot open").arg(fname);
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

	QString msg = tr("Modified %1 TIM file(s), replaced %2 image(s) "
					 "(non-TIM skipped: %3, images not found: %4, normalized: %5).")
		.arg(changedFiles).arg(replaced).arg(skipped).arg(missing).arg(normalized);
	if (stpLossCount > 0) {
		msg += tr("\n\nWARNING: in %1 file(s) the BMP replacement stripped STP "
				  "bits in 16bpp groups. Use PNG instead.").arg(stpLossCount);
	}
	emit finished(msg, errors, sizeMismatches, openedFileRewritten);
}