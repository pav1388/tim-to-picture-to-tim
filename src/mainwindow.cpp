// ttptt — TIM <-> PNG converter for PlayStation 1
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "timcodec.h"
#include "palettes.h"
#include "logwindow.h"
#include "placeholder.h"
#include "aboutdialog.h"
#include "themes.h"

#ifdef Q_OS_WIN
#  include <shlobj.h>
#endif

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
// #include <QSize>
#include <QColor>
// #include <QDialog>
#include <QInputDialog>
#include <QColorDialog>
#include <QDir>
#include <QPalette>
// #include <QDialogButtonBox>
#include <QLabel>
#include <QPainter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QDebug>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
// #include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
// #include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QFontMetrics>
#include <cstring>

static const QStringList kBppLabels = {"0 (4-bit)", "1 (8-bit)", "2 (16-bit)", "3 (24-bit)"};
static const int kRecentLimit = 10;
struct FileAssocInfo {
	const char *ext;
	const char *progId;
	const char *friendly;
	const char *contentType;
	bool		isImage;
};

static const FileAssocInfo kFileAssocs[] = {
	{ ".tim",   "ttptt.tim",	 "TIM (PlayStation Texture Image)", "application/x-tim-image",	true  },
	{ ".ttptt", "ttptt.project", "ttptt Project",				   "application/x-ttptt-project", false },
};

// =====================================================================
// Helper: QMessageBox с русскими подписями стандартных кнопок
// =====================================================================
static QMessageBox::StandardButton showMsgBox(
	QWidget *parent, QMessageBox::Icon icon,
	const QString &title, const QString &text,
	QMessageBox::StandardButtons buttons = QMessageBox::Ok,
	QMessageBox::StandardButton defaultBtn = QMessageBox::NoButton)
{
	QMessageBox box(parent);
	box.setIcon(icon);
	box.setWindowTitle(title);
	box.setText(text);
	box.setStandardButtons(buttons);
	if (defaultBtn != QMessageBox::NoButton) box.setDefaultButton(defaultBtn);

	struct BtnLabel { QMessageBox::StandardButton b; const char *t; };
	static const BtnLabel kLabels[] = {
		{ QMessageBox::Ok,	  "OK" }, { QMessageBox::Yes,	 "Да" },
		{ QMessageBox::No,	  "Нет" }, { QMessageBox::Save,	"Сохранить" },
		{ QMessageBox::Discard, "Не сохранять" }, { QMessageBox::Cancel,  "Отмена" },
		{ QMessageBox::Close,   "Закрыть" }, { QMessageBox::Abort,   "Прервать" },
		{ QMessageBox::Retry,   "Повторить" }, { QMessageBox::Ignore,  "Пропустить" },
	};
	for (const auto &l : kLabels)
		if (auto *b = box.button(l.b)) b->setText(QString::fromUtf8(l.t));

	return static_cast<QMessageBox::StandardButton>(box.exec());
}

static bool parseFlexible(const QString &sIn, int *out, QString *err = nullptr) {
	QString s = sIn.trimmed();
	if (s.isEmpty()) { if (err) *err = "Пустое значение"; return false; }
	bool ok = false;
	int v = s.toInt(&ok, 0);
	if (!ok) { if (err) *err = "Не число: " + sIn; return false; }
	*out = v;
	return true;
}

// STP-детекция для 16bpp (BMP не хранит альфу)
static bool imageHasStpPixels(const Tim::Image &im, int bpp) {
	if (bpp != 2) return false;
	const QByteArray &d = im.data;
	const uchar *p = reinterpret_cast<const uchar*>(d.constData());
	int n = d.size() / 2;
	for (int i = 0; i < n; ++i)
		if (Tim::rd16(p + i * 2) & 0x8000) return true;
	return false;
}

static bool groupHasAnyStp(const Tim::Group &g) {
	if (g.bpp != 2) return false;
	for (const auto &im : g.images)
		if (imageHasStpPixels(im, g.bpp)) return true;
	return false;
}

// Запись метаданных .ttptt
static bool writeTtpttFile(const QString &path, const QVector<Tim::Group> &groups,
						   const QString &sourcePath = QString(),
						   int paletteRow = 0, int currentEntry = 0,
						   bool zoomFit = true, bool showStp = false) {
	if (groups.isEmpty()) return false;

	QJsonObject root;
	root["version"]	  = 1;
	root["app"]		  = "ttptt";
	if (!sourcePath.isEmpty())
		root["sourcePath"] = sourcePath;
	root["paletteRow"]   = paletteRow;
	root["currentEntry"] = currentEntry;
	root["zoomFit"]	  = zoomFit;
	root["showStp"]	  = showStp;

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

// Поиск изображения по соглашению об именах (.png предпочтительнее .bmp).
static QString findTimPng(const QString &dirPath, const QString &stem,
						  int globalIdx, bool singleImage,
						  bool allowPalFallback)
{
	QDir dir(dirPath);
	QStringList candidates;
	if (singleImage) {
		candidates << (stem + ".png");
		candidates << (stem + ".bmp");
	}
	candidates << QString("%1_img%2.png").arg(stem).arg(globalIdx, 2, 10, QChar('0'));
	candidates << QString("%1_img%2.bmp").arg(stem).arg(globalIdx, 2, 10, QChar('0'));
	candidates << QString("%1_img%2.png").arg(stem).arg(globalIdx);
	candidates << QString("%1_img%2.bmp").arg(stem).arg(globalIdx);
	if (allowPalFallback) {
		candidates << QString("%1_img%2_pal01.png").arg(stem).arg(globalIdx, 2, 10, QChar('0'));
		candidates << QString("%1_img%2_pal01.bmp").arg(stem).arg(globalIdx, 2, 10, QChar('0'));
	}

	for (const QString &cand : candidates) {
		const QString full = dir.filePath(cand);
		if (QFileInfo::exists(full)) return full;
	}
	return QString();
}

// =====================================================================
// Batch Worker Implementation
// =====================================================================
static QStringList autoSavePalettesHelper(const Tim::Group &g, const QString &baseNoext, bool act, bool pal, bool gpl) {
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

void BatchWorker::runExtract(const QString &folder, bool anyPal, bool autoAct,
							 bool autoPal, bool autoGpl, bool saveAllPalettes,
							 bool saveMeta, const QString &imageExt) {
	QDir dir(folder);
	QStringList files = dir.entryList(QDir::Files);
	files.sort();
	int count = 0, timFiles = 0, skipped = 0, metaSaved = 0;
	QStringList errors;
	QStringList metaErrors;
	const QString ext = imageExt.startsWith('.') ? imageExt : ("." + imageExt);

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
			if (writeTtpttFile(metaPath, groups, dir.filePath(fname), 0, 0, true, false))
				++metaSaved;
			else
				metaErrors << fname + ": метаданные не сохранены";
		}

		int n = 0;
		for (const Tim::Group &g : groups) {
			for (const QString &w : g.warnings) emit logMsg(fname + ": " + w);
			for (int i = 0; i < g.images.size(); ++i) {
				++n;
				QString imgName = QString("%1_img%2%3").arg(stem).arg(n, 2, 10, QChar('0')).arg(ext);
				QString imgPath = dir.filePath(imgName);
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

				if (anyPal) {
					QString baseNoext = dir.filePath(QString("%1_img%2").arg(stem).arg(n, 2, 10, QChar('0')));
					autoSavePalettesHelper(g, baseNoext, autoAct, autoPal, autoGpl);
				}

				if (saveAllPalettes && (g.bpp == 0 || g.bpp == 1) &&
					g.hasClut && g.clutHeader.height > 1) {
					int ch = qMax<int>(1, g.clutHeader.height);
					for (int r = 1; r < ch; ++r) {
						QImage pimg = Tim::decodeImage(g, i, r, -1);
						if (pimg.isNull()) continue;
						QString ppath = dir.filePath(QString("%1_img%2_pal%3%4")
							.arg(stem).arg(n, 2, 10, QChar('0'))
							.arg(r, 2, 10, QChar('0'))
							.arg(ext));
						if (pimg.save(ppath)) ++count;
					}
				}
			}
		}
	}

	QString msg = QString("Экспортировано %1 файлов (%2) из %3 TIM-файлов "
						  "(пропущено не-TIM: %4).")
		.arg(count).arg(ext).arg(timFiles).arg(skipped);
	if (saveMeta)
		msg += QString(" Метаданных: %1.").arg(metaSaved);
	if (!metaErrors.isEmpty())
		errors.append(metaErrors);
	emit finished(msg, errors, QStringList(), false);
}

void BatchWorker::runReplace(const QString &folder, const QString &currentSourcePath) {
	QDir dir(folder);
	QStringList files = dir.entryList(QDir::Files);
	files.sort();
	int replaced = 0, missing = 0, timFiles = 0, changedFiles = 0, skipped = 0, normalized = 0;
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

				const QString pngPath = findTimPng(dir.path(), stem, n, false, false);
				if (pngPath.isEmpty()) { ++missing; continue; }
				const QString imgName = QFileInfo(pngPath).fileName();

				QImage im(pngPath);
				if (im.isNull()) {
					errors << imgName + " → " + fname + ": open failed";
					continue;
				}
				if (im.width() != w || im.height() != h) {
					sizeMismatches << QString("%1 → %2: image %3x%4, TIM %5x%6")
						.arg(imgName, fname)
						.arg(im.width()).arg(im.height()).arg(w).arg(h);
				}
				im = Tim::normalizeImageForTim(im, w, h);
				if (!Tim::applyEncodedToImage(g, i, im, 0)) {
					errors << imgName + " → " + fname + ": encode failed";
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
			emit logMsg(QString("%1: нормализован %2 → %3")
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
						  "(пропущено не-TIM: %3, не найдено PNG: %4, нормализовано: %5).")
		.arg(changedFiles).arg(replaced).arg(skipped).arg(missing).arg(normalized);
	emit finished(msg, errors, sizeMismatches, openedFileRewritten);
}

// =====================================================================
// MainWindow Implementation
// =====================================================================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
	setWindowTitle(QString("ttptt: tim-to-png-to-tim v%1").arg(QCoreApplication::applicationVersion()));
	setAcceptDrops(true);
	setWindowIcon(QIcon(":/icon.ico"));
	m_settings = new QSettings(QCoreApplication::applicationDirPath() + "/ttptt-settings.ini", QSettings::IniFormat, this);
	loadConfig();
	m_logWindow = new LogWindow(nullptr);
	buildMenus();
	buildUi();
	if (m_showLog && m_logWindow) {
		m_logWindow->show();
		if (m_logAction) m_logAction->setChecked(true);
	}
	if (m_logWindow) {
		connect(m_logWindow, &LogWindow::closed, this, [this]() {
			if (m_shuttingDown) return;
			m_showLog = false;
			if (m_logAction) m_logAction->setChecked(false);
		});
	}
	applyTheme(m_darkTheme);
	updateTitle();
	updateNavButtons();
	resize(680, 480);
	restoreGeometry(m_settings->value("window_geometry").toByteArray());
	refreshPreview();
	setStatus(tr("Готово."));
	qInfo() << "Приложение готово";
}

MainWindow::~MainWindow() {
	if (m_batchThread) {
		m_batchThread->quit();
		m_batchThread->wait();
	}
	delete m_logWindow;
	m_logWindow = nullptr;
}

void MainWindow::loadConfig() {
	m_recentFiles = m_settings->value("recent_files").toStringList().mid(0, kRecentLimit);
	m_zoomFit = m_settings->value("zoom_fit", true).toBool();
	m_showStp = m_settings->value("show_transparency", false).toBool();
	m_darkTheme = m_settings->value("theme", "light").toString() == "dark";
	m_autoSaveAct = m_settings->value("auto_save_act_with_png", true).toBool();
	m_autoSavePal = m_settings->value("auto_save_pal_with_png", false).toBool();
	m_autoSaveGpl = m_settings->value("auto_save_gpl_with_png", false).toBool();
	m_batchSaveAct = m_settings->value("batch_save_act_with_png", false).toBool();
	m_batchSavePal = m_settings->value("batch_save_pal_with_png", false).toBool();
	m_batchSaveGpl = m_settings->value("batch_save_gpl_with_png", false).toBool();
	m_showLog = m_settings->value("show_log", false).toBool();
	m_saveAllPalettesBatch = m_settings->value("batch_save_all_palettes", false).toBool();
	m_saveAllPalettesAuto = m_settings->value("auto_save_all_palettes", false).toBool();
	m_batchSaveTtptt = m_settings->value("batch_save_ttptt", false).toBool();
	m_autoSaveTtptt = m_settings->value("auto_save_ttptt", true).toBool();
	m_lastDir = m_settings->value("last_dir").toString();
	m_batchImageFormat = m_settings->value("batch_image_format", "png").toString();
	if (m_batchImageFormat != "bmp" && m_batchImageFormat != "png") m_batchImageFormat = "png";
}

void MainWindow::saveConfig() {
	m_settings->setValue("theme", m_darkTheme ? "dark" : "light");
	m_settings->setValue("show_log", m_showLog);
	m_settings->setValue("zoom_fit", m_zoomFit);
	m_settings->setValue("show_transparency", m_showStp);
	m_settings->setValue("auto_save_act_with_png", m_autoSaveAct);
	m_settings->setValue("auto_save_pal_with_png", m_autoSavePal);
	m_settings->setValue("auto_save_gpl_with_png", m_autoSaveGpl);
	m_settings->setValue("batch_save_act_with_png", m_batchSaveAct);
	m_settings->setValue("batch_save_pal_with_png", m_batchSavePal);
	m_settings->setValue("batch_save_gpl_with_png", m_batchSaveGpl);
	m_settings->setValue("batch_image_format", m_batchImageFormat);
	m_settings->setValue("batch_save_all_palettes", m_saveAllPalettesBatch);
	m_settings->setValue("auto_save_all_palettes", m_saveAllPalettesAuto);
	m_settings->setValue("auto_save_ttptt", m_autoSaveTtptt);
	m_settings->setValue("batch_save_ttptt", m_batchSaveTtptt);
	m_settings->setValue("last_dir", m_lastDir);
	m_settings->setValue("recent_files", m_recentFiles);
	m_settings->setValue("window_geometry", saveGeometry());
	m_settings->sync();
}

void MainWindow::rememberDir(const QString &path) {
	if (!path.isEmpty()) m_lastDir = QFileInfo(path).absolutePath();
}

QString MainWindow::initialDir() const { return m_lastDir; }

void MainWindow::addRecentFile(const QString &path) {
	if (path.isEmpty()) return;
	QString abs = QFileInfo(path).absoluteFilePath();
	if (!QFileInfo::exists(abs)) return;
	m_recentFiles.removeAll(abs);
	m_recentFiles.prepend(abs);
	while (m_recentFiles.size() > kRecentLimit) m_recentFiles.removeLast();
	updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
	if (!m_recentSubmenu) return;
	m_recentSubmenu->clear();
	if (m_recentFiles.isEmpty()) {
		m_recentSubmenu->addAction(tr("(нет недавних файлов)"))->setEnabled(false);
		return;
	}
	for (int i = 0; i < m_recentFiles.size(); ++i) {
		QString path = m_recentFiles[i];
		QAction *a = m_recentSubmenu->addAction(QString("%1. %2").arg(i + 1).arg(QFileInfo(path).fileName()));
		connect(a, &QAction::triggered, this, [this, path]() {
			if (!QFileInfo::exists(path)) {
				showMsgBox(this, QMessageBox::Warning, tr("Файл не найден"), path);
				m_recentFiles.removeAll(path);
				updateRecentMenu();
				return;
			}
			if (!confirmDiscard()) return;
			if (QFileInfo(path).suffix().toLower() == "ttptt") {
				loadTtpttFromPath(path, true);
			} else {
				loadTimFromPath(path, true);
			}
		});
	}
	m_recentSubmenu->addSeparator();
	connect(m_recentSubmenu->addAction(tr("Очистить список")), &QAction::triggered, this, &MainWindow::clearRecent);
}

void MainWindow::buildMenus() {
	auto *mb = menuBar();
	m_fileMenu = mb->addMenu(tr("Файл"));
	m_fileMenu->addAction(tr("Открыть TIM/Метаданные..."), this, &MainWindow::openTim, QKeySequence("Ctrl+O"));
	m_fileMenu->addAction(tr("Открыть как сырой TIM..."), this, &MainWindow::openRawTim, QKeySequence("Ctrl+Shift+O"));
	m_fileMenu->addAction(tr("Заменить текущее изображение..."), this, &MainWindow::replaceImage, QKeySequence("Ctrl+R"));
	m_fileMenu->addSeparator();
	m_fileMenu->addAction(tr("Сохранить TIM"), this, &MainWindow::saveTim, QKeySequence("Ctrl+S"));
	m_fileMenu->addAction(tr("Сохранить TIM как..."), this, &MainWindow::saveTimAs);
	m_fileMenu->addSeparator();
	m_fileMenu->addAction(tr("Сохранить изображение как PNG/BMP..."), this, &MainWindow::savePng, QKeySequence("Ctrl+Shift+S"));
	m_fileMenu->addAction(tr("Сохранить все изображения как PNG/BMP..."), this, &MainWindow::saveAllPngs,
						  QKeySequence("Ctrl+Shift+A"));
	m_fileMenu->addSeparator();
	m_saveTtpttAction = m_fileMenu->addAction(tr("Сохранить метаданные (.ttptt)"), this, &MainWindow::saveTtptt,
											  QKeySequence("Ctrl+Alt+S"));
	m_saveTtpttAsAction = m_fileMenu->addAction(tr("Сохранить метаданные как..."),
												this, &MainWindow::saveTtpttAs);
	m_autoSaveTtpttAction = m_fileMenu->addAction(tr("Сохранять метаданные .ttptt рядом с PNG/BMP"));
	m_autoSaveTtpttAction->setCheckable(true);
	m_autoSaveTtpttAction->setChecked(m_autoSaveTtptt);
	connect(m_autoSaveTtpttAction, &QAction::toggled, this, [this](bool v) {
		m_autoSaveTtptt = v;
	});
	m_fileMenu->addSeparator();
	m_fileMenu->addAction(tr("Закрыть файл"), this, &MainWindow::closeFile, QKeySequence("Ctrl+W"));
	m_fileMenu->addSeparator();
	m_recentSubmenu = m_fileMenu->addMenu(tr("Недавние файлы"));
	updateRecentMenu();
	m_fileMenu->addSeparator();
	m_fileMenu->addAction(tr("Выход"), this, &QWidget::close);

	auto *em = mb->addMenu(tr("Правка"));
	em->addAction(tr("Применить метаданные"), this, &MainWindow::applyMetadata, QKeySequence("Ctrl+Return"));
	em->addAction(tr("Сбросить изменения"), this, &MainWindow::resetMetadata, QKeySequence("Ctrl+Shift+R"));
	em->addSeparator();
	em->addAction(tr("Конвертировать 24bpp → 16bpp"), this, &MainWindow::convertTo16bpp);
	em->addAction(tr("Конвертировать 16bpp → 24bpp"), this, &MainWindow::convertTo24bpp);
	em->addSeparator();
	em->addAction(tr("Разделить изображение на два"), this, &MainWindow::splitImage);
	em->addAction(tr("Слить со следующим"), this, &MainWindow::mergeWithNext);
	em->addAction(tr("Слить с предыдущим"), this, &MainWindow::mergeWithPrev);

	auto *pm = mb->addMenu(tr("Палитра"));
	pm->addAction(tr("Экспорт палитры..."), this, &MainWindow::exportPalette);
	pm->addAction(tr("Импорт палитры..."), this, &MainWindow::importPalette);
	pm->addSeparator();
	m_autoActAction = pm->addAction(tr("Сохранять ACT рядом с изображением"));
	m_autoActAction->setCheckable(true);
	m_autoActAction->setChecked(m_autoSaveAct);
	connect(m_autoActAction, &QAction::toggled, this, [this](bool v) { m_autoSaveAct = v; });

	m_autoPalAction = pm->addAction(tr("Сохранять PAL рядом с изображением"));
	m_autoPalAction->setCheckable(true);
	m_autoPalAction->setChecked(m_autoSavePal);
	connect(m_autoPalAction, &QAction::toggled, this, [this](bool v) { m_autoSavePal = v; });

	m_autoGplAction = pm->addAction(tr("Сохранять GPL рядом с изображением"));
	m_autoGplAction->setCheckable(true);
	m_autoGplAction->setChecked(m_autoSaveGpl);
	connect(m_autoGplAction, &QAction::toggled, this, [this](bool v) { m_autoSaveGpl = v; });

	pm->addSeparator();
	m_manualAllPalettesAction = pm->addAction(tr("Сохранять все изображения в каждой палитре"));
	m_manualAllPalettesAction->setCheckable(true);
	m_manualAllPalettesAction->setChecked(m_saveAllPalettesAuto);
	connect(m_manualAllPalettesAction, &QAction::toggled, this, [this](bool v) { m_saveAllPalettesAuto = v; });

	auto *vm = mb->addMenu(tr("Вид"));
	m_stpAction = vm->addAction(tr("Прозрачность (STP)"), this, &MainWindow::toggleStp);
	m_stpAction->setCheckable(true);
	m_stpAction->setChecked(m_showStp);
	vm->addSeparator();
	vm->addAction(tr("Предыдущее изображение"), this, &MainWindow::prevImage, QKeySequence("Ctrl+Left"));
	vm->addAction(tr("Следующее изображение"), this, &MainWindow::nextImage, QKeySequence("Ctrl+Right"));
	vm->addAction(tr("Предыдущая палитра"), this, &MainWindow::prevPaletteRow, QKeySequence("Ctrl+Down"));
	vm->addAction(tr("Следующая палитра"), this, &MainWindow::nextPaletteRow, QKeySequence("Ctrl+Up"));
	vm->addSeparator();
	vm->addAction(tr("Масштаб 1:1"), this, &MainWindow::previewScale1to1, QKeySequence("Ctrl+1"));
	vm->addAction(tr("Вписать в окно"), this, &MainWindow::previewScaleFit, QKeySequence("Ctrl+2"));
	vm->addSeparator();
	m_themeLightAction = vm->addAction(tr("Светлая тема"), this, &MainWindow::setThemeLight);
	m_themeLightAction->setCheckable(true);
	m_themeLightAction->setChecked(!m_darkTheme);
	m_themeDarkAction = vm->addAction(tr("Тёмная тема"), this, &MainWindow::setThemeDark);
	m_themeDarkAction->setCheckable(true);
	m_themeDarkAction->setChecked(m_darkTheme);
	vm->addSeparator();
	
	auto *bm = mb->addMenu(tr("Пакетная обработка"));
	bm->addAction(tr("Пакетное извлечение PNG/BMP из TIM"), this, &MainWindow::batchExtract);
	bm->addAction(tr("Пакетная замена изображений в TIM"), this, &MainWindow::batchReplace);
	bm->addSeparator();
	m_batchActAction = bm->addAction(tr("Сохранять палитру ACT рядом с изображением"));
	m_batchActAction->setCheckable(true);
	m_batchActAction->setChecked(m_batchSaveAct);
	connect(m_batchActAction, &QAction::toggled, this, [this](bool v) { m_batchSaveAct = v; });
	m_batchPalAction = bm->addAction(tr("Сохранять палитру PAL рядом с изображением"));
	m_batchPalAction->setCheckable(true);
	m_batchPalAction->setChecked(m_batchSavePal);
	connect(m_batchPalAction, &QAction::toggled, this, [this](bool v) { m_batchSavePal = v; });
	m_batchGplAction = bm->addAction(tr("Сохранять палитру GPL рядом с изображением"));
	m_batchGplAction->setCheckable(true);
	m_batchGplAction->setChecked(m_batchSaveGpl);
	connect(m_batchGplAction, &QAction::toggled, this, [this](bool v) { m_batchSaveGpl = v; });
	bm->addSeparator();
	m_batchTtpttAction = bm->addAction(tr("Сохранять метаданные .ttptt рядом с изображением"));
	m_batchTtpttAction->setCheckable(true);
	m_batchTtpttAction->setChecked(m_batchSaveTtptt);
	connect(m_batchTtpttAction, &QAction::toggled, this, [this](bool v) { m_batchSaveTtptt = v; });
	bm->addSeparator();
	m_batchAllPalettesAction = bm->addAction(tr("Сохранять все изображения в каждой палитре"));
	m_batchAllPalettesAction->setCheckable(true);
	m_batchAllPalettesAction->setChecked(m_saveAllPalettesBatch);
	connect(m_batchAllPalettesAction, &QAction::toggled, this, [this](bool v) { m_saveAllPalettesBatch = v; });

	auto *mm = mb->addMenu(tr("Разное"));
	m_logAction = mm->addAction(tr("Журнал логов"), this, &MainWindow::toggleLogWindow);
	m_logAction->setCheckable(true);
	m_logAction->setChecked(m_logWindow && m_logWindow->isVisible());
	mm->addSeparator();
	mm->addAction(tr("Ассоциация .tim с программой"), this,
				  [this]() { registerFileAssociation(".tim"); });
	mm->addAction(tr("Ассоциация .ttptt с программой"), this,
				  [this]() { registerFileAssociation(".ttptt"); });
	mm->addSeparator();
	mm->addAction(tr("Снять ассоциацию .tim"), this,
				  [this]() { unregisterFileAssociation(".tim"); });
	mm->addAction(tr("Снять ассоциацию .ttptt"), this,
				  [this]() { unregisterFileAssociation(".ttptt"); });

	QAction *aboutAct = mb->addAction(tr("О программе…"));
	connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout() { ::showAboutDialog(this); }

void MainWindow::buildUi() {
	auto *central = new QWidget(this); setCentralWidget(central);
	auto *main = new QHBoxLayout(central);
	auto *left = new QVBoxLayout(); main->addLayout(left, 1);
	
	// --- Палитра ---
	auto *palHead = new QHBoxLayout();
	auto *palLabel = new QLabel(tr("Палитра:"));
	palLabel->setFixedWidth(110);
	palLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	palHead->addWidget(palLabel, 0);
	
	m_palInfoLabel = new QLabel(""); 
	m_palInfoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	palHead->addWidget(m_palInfoLabel, 1);
	
	m_palPrevBtn = new QPushButton("<-"); m_palPrevBtn->setMaximumWidth(44); 
	m_palPrevBtn->setMinimumHeight(36);
	m_palRowLabel = new QLabel("—"); m_palRowLabel->setAlignment(Qt::AlignCenter); m_palRowLabel->setFixedWidth(50);
	m_palNextBtn = new QPushButton("->"); m_palNextBtn->setMaximumWidth(44); 
	m_palNextBtn->setMinimumHeight(36);
	
	palHead->removeWidget(m_palInfoLabel);
	palHead->addWidget(m_palPrevBtn, 0);
	palHead->addWidget(m_palRowLabel, 0);
	palHead->addWidget(m_palNextBtn, 0);
	palHead->addWidget(m_palInfoLabel, 1); 

	connect(m_palPrevBtn, &QPushButton::clicked, this, &MainWindow::prevPaletteRow);
	connect(m_palNextBtn, &QPushButton::clicked, this, &MainWindow::nextPaletteRow);
	left->addLayout(palHead);

	m_paletteLabel = new QLabel(); m_paletteLabel->setMinimumHeight(36); m_paletteLabel->setMaximumHeight(36);
	m_paletteLabel->setAutoFillBackground(true); 
	m_paletteLabel->setBackgroundRole(QPalette::Window);
	m_paletteLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_paletteLabel->installEventFilter(this);
	left->addWidget(m_paletteLabel);

	// --- Изображение ---
	auto *prevHead = new QHBoxLayout();
	auto *prevLabel = new QLabel(tr("Изображение:"));
	prevLabel->setFixedWidth(110);
	prevLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	prevHead->addWidget(prevLabel, 0);

	m_imgPrevBtn = new QPushButton("<-"); m_imgPrevBtn->setMaximumWidth(44); 
	m_imgPrevBtn->setMinimumHeight(36);
	m_navLabel = new QLabel("—"); m_navLabel->setAlignment(Qt::AlignCenter); m_navLabel->setFixedWidth(50);
	m_imgNextBtn = new QPushButton("->"); m_imgNextBtn->setMaximumWidth(44); 
	m_imgNextBtn->setMinimumHeight(36);
	
	prevHead->addWidget(m_imgPrevBtn, 0);
	prevHead->addWidget(m_navLabel, 0);
	prevHead->addWidget(m_imgNextBtn, 0);
	prevHead->addStretch();

	auto *btnStp = new QPushButton("STP"); 
	btnStp->setCheckable(true); 
	btnStp->setChecked(m_showStp);
	btnStp->setMinimumHeight(36);
	btnStp->setFixedWidth(44);
	auto *btnFit = new QPushButton("[ ]"); 
	btnFit->setMinimumHeight(36);
	btnFit->setFixedWidth(44);
	auto *btn11 = new QPushButton("1:1"); 
	btn11->setMinimumHeight(36);
	btn11->setFixedWidth(44);
	connect(btn11, &QPushButton::clicked, this, &MainWindow::previewScale1to1);
	connect(btnFit, &QPushButton::clicked, this, &MainWindow::previewScaleFit);
	connect(btnStp, &QPushButton::clicked, this, &MainWindow::toggleStp);
	prevHead->addWidget(btnStp);
	prevHead->addWidget(btnFit);
	prevHead->addWidget(btn11);

	connect(m_imgPrevBtn, &QPushButton::clicked, this, &MainWindow::prevImage);
	connect(m_imgNextBtn, &QPushButton::clicked, this, &MainWindow::nextImage);
	left->addLayout(prevHead);

	m_previewLabel = new QLabel(); m_previewLabel->setAlignment(Qt::AlignCenter);
	m_previewLabel->setMinimumSize(200, 200);
	m_previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	m_previewLabel->setAutoFillBackground(true); 
	m_previewLabel->setBackgroundRole(QPalette::Window);
	m_previewLabel->installEventFilter(this);
	left->addWidget(m_previewLabel, 1);

	// --- Правая панель (Параметры) ---
	auto *right = new QWidget(); right->setMaximumWidth(380); main->addWidget(right);
	buildParamsPanel(right);

	m_statusLabel = new QLabel(tr("Готово.")); statusBar()->addWidget(m_statusLabel, 1);
}

void MainWindow::buildParamsPanel(QWidget *parent) {
	auto *grid = new QGridLayout(parent);
	grid->setColumnStretch(0, 1);
	grid->setColumnStretch(1, 1);
	grid->setSpacing(4);
	grid->setContentsMargins(2, 2, 2, 2);

	// --- Заголовок TIM (Колонка 0, Ряд 1) ---
	auto *f1 = new QGroupBox(tr("Заголовок"), parent); 
	auto *l1 = new QFormLayout(f1);
	l1->setVerticalSpacing(2);
	l1->setContentsMargins(4, 6, 4, 6);

	m_bppCombo = new QComboBox(); 
	for (auto &s : kBppLabels) m_bppCombo->addItem(s); 
	m_bppCombo->setMinimumWidth(80);
	l1->addRow("bpp:", m_bppCombo);

	m_hasClutCheck = new QCheckBox("has_clut"); 
	m_hasImageCheck = new QCheckBox("has_image");
	l1->addRow(m_hasClutCheck); 
	l1->addRow(m_hasImageCheck);

	m_flagsDisplay = new QLabel("0x00"); 
	m_flagsDisplay->setStyleSheet("color: #888;");
	l1->addRow("flags:", m_flagsDisplay);
	grid->addWidget(f1, 1, 0);

	connect(m_bppCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::updateFlagsDisplay);
	connect(m_hasClutCheck, &QCheckBox::toggled, this, &MainWindow::updateFlagsDisplay);
	connect(m_hasImageCheck, &QCheckBox::toggled, this, &MainWindow::updateFlagsDisplay);

	// --- CLUT (Колонка 0, Ряд 2) ---
	auto *f2 = new QGroupBox(tr("CLUT"), parent); 
	auto *l2 = new QFormLayout(f2);
	l2->setVerticalSpacing(2);
	l2->setContentsMargins(4, 6, 4, 6);

	m_clutSizeEdit = new QLineEdit(); m_clutSizeEdit->setReadOnly(true); m_clutSizeEdit->setMinimumWidth(80);
	m_clutXEdit = new QLineEdit(); m_clutXEdit->setMinimumWidth(80);
	m_clutYEdit = new QLineEdit(); m_clutYEdit->setMinimumWidth(80);
	m_clutWidthEdit = new QLineEdit(); m_clutWidthEdit->setMinimumWidth(80);
	m_clutHeightEdit = new QLineEdit(); m_clutHeightEdit->setMinimumWidth(80);

	l2->addRow("size:", m_clutSizeEdit); 
	l2->addRow("x:", m_clutXEdit); 
	l2->addRow("y:", m_clutYEdit);
	l2->addRow("width_words:", m_clutWidthEdit); 
	l2->addRow("height:", m_clutHeightEdit);

	QLabel *hint2 = new QLabel(tr("(size = 12+w_w*2*h)")); 
	hint2->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	hint2->setStyleSheet("color: #888; font-size: 9pt;"); 
	l2->addRow(hint2);
	grid->addWidget(f2, 2, 0);

	connect(m_clutWidthEdit, &QLineEdit::textChanged, this, &MainWindow::onClutDimsChanged);
	connect(m_clutHeightEdit, &QLineEdit::textChanged, this, &MainWindow::onClutDimsChanged);

	// --- Изображение (Колонка 1, Ряды 1-2) ---
	auto *f3 = new QGroupBox(tr("Изображение"), parent); 
	auto *l3 = new QFormLayout(f3);
	l3->setVerticalSpacing(2);
	l3->setContentsMargins(4, 6, 4, 6);

	m_imgSizeEdit = new QLineEdit(); m_imgSizeEdit->setReadOnly(true); m_imgSizeEdit->setMinimumWidth(80);
	m_imgXEdit = new QLineEdit(); m_imgXEdit->setMinimumWidth(80);
	m_imgYEdit = new QLineEdit(); m_imgYEdit->setMinimumWidth(80);
	m_imgWidthEdit = new QLineEdit(); m_imgWidthEdit->setMinimumWidth(80);
	m_imgHeightEdit = new QLineEdit(); m_imgHeightEdit->setMinimumWidth(80);

	l3->addRow("size:", m_imgSizeEdit); 
	l3->addRow("x:", m_imgXEdit); 
	l3->addRow("y:", m_imgYEdit);
	l3->addRow("width_words:", m_imgWidthEdit); 
	l3->addRow("height:", m_imgHeightEdit);

	QLabel *hint1 = new QLabel(tr("(size = 12+w_w*2*h)")); 
	hint1->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	hint1->setStyleSheet("color: #888; font-size: 9pt;"); 
	l3->addRow(hint1);

	m_imagePixelsLabel = new QLabel(""); 
	m_imagePixelsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	l3->addRow(m_imagePixelsLabel);
	
	grid->addWidget(f3, 1, 1, 2, 1); 

	connect(m_imgWidthEdit, &QLineEdit::textChanged, this, &MainWindow::onImageDimsChanged);
	connect(m_imgHeightEdit, &QLineEdit::textChanged, this, &MainWindow::onImageDimsChanged);

	// --- Кнопки (Ряд 3) ---
	auto *bReset = new QPushButton(tr("Сбросить изменения")); 
	auto *bApply = new QPushButton(tr("Применить метаданные"));
	bReset->setMinimumHeight(36);
	bApply->setMinimumHeight(36);

	connect(bReset, &QPushButton::clicked, this, &MainWindow::resetMetadata);
	connect(bApply, &QPushButton::clicked, this, &MainWindow::applyMetadata);
	
	grid->addWidget(bReset, 3, 0);
	grid->addWidget(bApply, 3, 1);
}

Tim::Group *MainWindow::currentGroup(int *imgIdx) {
	if (m_currentEntry < 0 || m_currentEntry >= m_entries.size()) { if (imgIdx) *imgIdx = -1; return nullptr; }
	auto p = m_entries[m_currentEntry]; if (imgIdx) *imgIdx = p.second;
	if (p.first < 0 || p.first >= m_groups.size()) return nullptr;
	return &m_groups[p.first];
}

const Tim::Group *MainWindow::currentGroup(int *imgIdx) const { return const_cast<MainWindow*>(this)->currentGroup(imgIdx); }

bool MainWindow::loadTimFromPath(const QString &path, bool silent) {
	if (QFileInfo(path).suffix().toLower() == "ttptt")
		return loadTtpttFromPath(path, silent);

	if (m_batchRunning) {
		if (!silent) showMsgBox(this, QMessageBox::Information, tr("Инфо"), tr("Идет пакетная операция."));
		return false;
	}
	if (!silent && !confirmDiscard()) return false;
	qInfo() << "Открытие TIM:" << path;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		qWarning() << "Не удалось открыть:" << path;
		if (!silent) showMsgBox(this, QMessageBox::Critical, "Ошибка", "Не могу открыть файл");
		return false;
	}
	QByteArray data = f.readAll(); f.close();
	QVector<Tim::Group> groups = Tim::parseAll(data);
	if (groups.isEmpty()) {
		qWarning() << "Не TIM:" << path;
		if (!silent) showMsgBox(this, QMessageBox::Critical, "Ошибка", "Не TIM");
		return false;
	}
	m_groups = groups;
	m_entries.clear();
	for (int gi = 0; gi < m_groups.size(); ++gi) {
		if (m_groups[gi].images.isEmpty()) m_entries.append({ gi, -1 });
		else for (int ii = 0; ii < m_groups[gi].images.size(); ++ii)
			m_entries.append({ gi, ii });
	}
	m_currentEntry = m_entries.isEmpty() ? -1 : 0;
	m_paletteRow = 0;
	m_sourcePath = path;
	m_ttpttPath.clear();
	m_sourceStem = QFileInfo(path).completeBaseName();
	m_dirty = false;
	addRecentFile(path);
	rememberDir(path);
	refreshAll();
	updateTitle();
	setStatus(tr("Открыто: %1 | групп: %2 | изобр.: %3")
		.arg(QFileInfo(path).fileName()).arg(m_groups.size()).arg(m_entries.size()));
	if (!m_groups.isEmpty() && !m_groups.first().prefix.isEmpty())
		qInfo() << QString("Префикс перед первым TIM сохранён: %1 байт")
					   .arg(m_groups.first().prefix.size());
	qInfo() << QString("Открыто: %1 (групп: %2, изобр.: %3, размер: %4 байт)")
		.arg(QFileInfo(path).fileName()).arg(m_groups.size()).arg(m_entries.size()).arg(data.size());
	for (const auto &g : m_groups)
		for (const auto &w : g.warnings) qWarning() << "TIM:" << w;
	return true;
}

bool MainWindow::loadRawTimFromPath(const QString &path, bool silent, bool skipConfirm) {
	if (m_batchRunning) return false;
	if (!skipConfirm && !silent && !confirmDiscard()) return false;

	qInfo() << "Открытие как сырой TIM:" << QFileInfo(path).fileName();
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return false;
	QByteArray data = f.readAll();
	f.close();

	if (data.startsWith(QByteArray("TIM2", 4))) {
		showMsgBox(this, QMessageBox::Information, tr("TIM2"),
				   tr("Это TIM2 (PlayStation 2), а не PS1 TIM. Формат не поддерживается."));
		return false;
	}

	if (data.size() < 8) return false;

	Tim::Group group;
	int newOffset = 0;
	bool parseOk = Tim::parseGroupRaw(data, 0, &newOffset, &group);
	bool hasData = parseOk && (!group.images.isEmpty() || (group.hasClut && !group.clut.isEmpty()));

	QVector<Tim::Group> groups;
	if (hasData) {
		group.warnings << "Сырой TIM";
		groups << group;
	} else {
		if (silent) return false;
		if (showMsgBox(this, QMessageBox::Question, "Не распознан", "Открыть как пустой?",
					   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) return false;

		Tim::Group g;
		g.flags = 0x18;
		g.bpp = 0;
		g.hasClut = true;
		g.hasImage = true;
		g.clutHeader.size = 44;
		g.clutHeader.x = 0;
		g.clutHeader.y = 480;
		g.clutHeader.widthWords = 16;
		g.clutHeader.height = 1;
		g.clut = QByteArray(32, '\0');

		Tim::Image im;
		im.header.size = 12;
		im.header.x = 0;
		im.header.y = 0;
		im.header.widthWords = 16;
		im.header.height = 1;
		g.images << im;

		g.extra = data;
		groups << g;
	}

	m_groups = groups;
	m_entries.clear();
	for (int gi = 0; gi < m_groups.size(); ++gi) {
		if (m_groups[gi].images.isEmpty())
			m_entries.append({gi, -1});
		else
			for (int ii = 0; ii < m_groups[gi].images.size(); ++ii)
				m_entries.append({gi, ii});
	}
	m_currentEntry = 0;
	m_paletteRow = 0;
	m_sourcePath = path;
	m_ttpttPath.clear();
	m_sourceStem = QFileInfo(path).completeBaseName();
	m_dirty = false;
	rememberDir(path);
	refreshAll();
	updateTitle();
	return true;
}

void MainWindow::openTim() {
	if (m_batchRunning) return;
	QString p = QFileDialog::getOpenFileName(this, "TIM / Метаданные", initialDir(),
		"TIM и метаданные (*.tim *.ttptt);;TIM (*.tim);;Метаданные ttptt (*.ttptt);;All (*.*)");
	if (p.isEmpty()) return;
	if (QFileInfo(p).suffix().toLower() == "ttptt")
		loadTtpttFromPath(p);
	else
		loadTimFromPath(p);
}

void MainWindow::openRawTim() { 
	if (m_batchRunning) return; 
	QString p = QFileDialog::getOpenFileName(this, "Raw", initialDir(), "All (*.*)"); 
	if (!p.isEmpty()) loadRawTimFromPath(p); 
}

void MainWindow::clearRecent() {
	m_recentFiles.clear();
	updateRecentMenu();
}

bool MainWindow::confirmDiscard() {
	if (m_batchRunning) {
		showMsgBox(this, QMessageBox::Information, "Инфо", "Идет операция.");
		return false;
	}
	if (!m_dirty) return true;

	auto r = showMsgBox(this, QMessageBox::Question, "Изменения", "Сохранить?",
						QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
						QMessageBox::Save);
	if (r == QMessageBox::Cancel) {
		qInfo() << "Действие отменено пользователем (несохранённые изменения)";
		return false;
	}
	if (r == QMessageBox::Save) {
		if (!m_sourcePath.isEmpty()) {
			saveTim();
		} else if (!m_ttpttPath.isEmpty()) {
			auto choice = showMsgBox(this, QMessageBox::Question, "Куда сохранять",
				"Нет исходного .tim.\n"
				"«Да» — перезаписать PNG рядом + метаданные\n"
				"«Нет» — сохранить как новый .tim",
				QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
				QMessageBox::Yes);
			if (choice == QMessageBox::Cancel) return false;
			if (choice == QMessageBox::Yes) {
				saveAllPngs();
				saveTtptt();
			} else {
				saveTimAs();
			}
		} else {
			saveTimAs();
		}
		return !m_dirty;
	}
	return true;
}

bool MainWindow::writeTimToPath(const QString &path) {
	QByteArray data = Tim::rebuildAll(m_groups);
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		showMsgBox(this, QMessageBox::Critical, "Ошибка",
				   "Не могу открыть на запись:\n" + path);
		return false;
	}
	f.write(data);
	f.close();
	m_dirty = false;
	updateTitle();
	setStatus("Сохранено: " + path);
	qInfo() << QString("Сохранено: %1 (%2 байт)").arg(path).arg(data.size());
	return true;
}

void MainWindow::saveTim() {
	if (m_batchRunning || m_groups.isEmpty()) return;
	if (m_sourcePath.isEmpty()) { saveTimAs(); return; }
	writeTimToPath(m_sourcePath);
}

void MainWindow::saveTimAs() {
	if (m_batchRunning || m_groups.isEmpty()) return;
	QString def = m_sourceStem.isEmpty() ? "untitled" : m_sourceStem + "_out";
	QString path = QFileDialog::getSaveFileName(this, "Save TIM", def, "TIM (*.tim)");
	if (path.isEmpty()) return;
	if (!writeTimToPath(path)) return;

	m_sourcePath = path;
	m_sourceStem = QFileInfo(path).completeBaseName();
	m_ttpttPath.clear();
	rememberDir(path);
}

int MainWindow::saveExtraPalettes(const Tim::Group &g, int imgIdx, int currentPalRow,
								   const QString &dirPath, const QString &namePrefix,
								   const QString &ext) {
	if (!m_saveAllPalettesAuto) return 0;
	if (!(g.bpp == 0 || g.bpp == 1)) return 0;
	if (!g.hasClut || g.clutHeader.height <= 1) return 0;

	int saved = 0;
	int ch = qMax<int>(1, g.clutHeader.height);
	for (int r = 0; r < ch; ++r) {
		if (r == currentPalRow) continue;
		QImage pimg = Tim::decodeImage(g, imgIdx, r, -1);
		if (pimg.isNull()) continue;
		QString ppath = dirPath + "/" + namePrefix +
						QString("_pal%1%2").arg(r, 2, 10, QChar('0')).arg(ext);
		if (pimg.save(ppath)) ++saved;
	}
	return saved;
}

void MainWindow::savePng() {
	if (m_batchRunning) return;
	int ii = -1;
	Tim::Group *g = currentGroup(&ii);
	if (!g) return;

	QImage img = Tim::decodeImage(*g, ii, m_paletteRow, -1);
	if (img.isNull()) return;

	QString def = (m_entries.size() > 1 && m_currentEntry >= 0)
		? QString("%1_img%2.png").arg(m_sourceStem).arg(m_currentEntry + 1, 2, 10, QChar('0'))
		: m_sourceStem + ".png";

	QString path = QFileDialog::getSaveFileName(this,
		tr("Сохранить изображение"),
		def,
		tr("PNG (*.png);;BMP (*.bmp)"));
	if (path.isEmpty()) return;

	const QString ext = "." + QFileInfo(path).suffix().toLower();

	if (ext == ".bmp" && g->bpp == 2 && imageHasStpPixels(g->images[ii], g->bpp)) {
		if (showMsgBox(this, QMessageBox::Warning, tr("BMP и STP"),
			tr("В изображении есть STP-пиксели (полупрозрачность).\n"
			   "BMP не хранит альфа-канал — при обратном TIM→BMP→TIM\n"
			   "STP-биты будут сброшены.\n\nПродолжить?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
			return;
	}

	if (!img.save(path)) {
		showMsgBox(this, QMessageBox::Critical, tr("Ошибка"),
				   tr("Не удалось сохранить:\n") + path);
		return;
	}
	rememberDir(path);

	QString base = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName();
	autoSavePalettesHelper(*g, base, m_autoSaveAct, m_autoSavePal, m_autoSaveGpl);

	saveExtraPalettes(*g, ii, m_paletteRow,
					  QFileInfo(path).absolutePath(),
					  QFileInfo(path).completeBaseName(),
					  ext);

	if (m_autoSaveTtptt) {
		QString ttpttPath = QFileInfo(path).absolutePath() + "/" + m_sourceStem + ".ttptt";
		if (writeTtpttFile(ttpttPath, m_groups, m_sourcePath, m_paletteRow,
						   m_currentEntry, m_zoomFit, m_showStp)) {
			m_ttpttPath = ttpttPath;
			qInfo() << "Метаданные сохранены рядом с изображением:" << ttpttPath;
		}
	}

	setStatus("Сохранено: " + path);
	qInfo() << "Сохранено:" << path;
}

void MainWindow::saveAllPngs() {
	if (m_batchRunning || m_groups.isEmpty()) return;

	// 1) Формат
	QStringList fmtItems;
	fmtItems << tr("PNG (*.png)") << tr("BMP (*.bmp)");
	bool ok = false;
	QString fmt = QInputDialog::getItem(this, tr("Формат изображений"),
		tr("Формат для сохранения:"), fmtItems, 0, false, &ok);
	if (!ok) return;
	const QString ext = fmt.contains("bmp", Qt::CaseInsensitive) ? ".bmp" : ".png";

	// 2) Предупреждение про STP в BMP
	if (ext == ".bmp") {
		int stpGroups = 0;
		for (const auto &g : m_groups)
			if (groupHasAnyStp(g)) ++stpGroups;
		if (stpGroups > 0) {
			if (showMsgBox(this, QMessageBox::Warning, tr("BMP и STP"),
				tr("В %1 группе(ах) есть 16bpp-изображения со STP-пикселями.\n"
				   "BMP не хранит альфа-канал — при обратном TIM→BMP→TIM\n"
				   "STP-биты будут сброшены.\n\nПродолжить?").arg(stpGroups),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
				return;
		}
	}

	// 3) Каталог
	QString dirPath;
	if (!m_sourcePath.isEmpty())
		dirPath = QFileInfo(m_sourcePath).absolutePath();
	else if (!m_ttpttPath.isEmpty())
		dirPath = QFileInfo(m_ttpttPath).absolutePath();
	else
		dirPath = QFileDialog::getExistingDirectory(this, tr("Куда сохранять изображения"), initialDir());
	if (dirPath.isEmpty()) return;

	QString stem = m_sourceStem;
	if (stem.isEmpty() && !m_ttpttPath.isEmpty())
		stem = QFileInfo(m_ttpttPath).completeBaseName();
	if (stem.isEmpty()) stem = "untitled";

	QDir dir(dirPath);

	struct Item { Tim::Group *g; int imgIdx; int globalIdx; };
	QVector<Item> items;
	int n = 0;
	for (int gi = 0; gi < m_groups.size(); ++gi)
		for (int ii = 0; ii < m_groups[gi].images.size(); ++ii) {
			++n;
			items.append({ &m_groups[gi], ii, n });
		}
	if (items.isEmpty()) {
		showMsgBox(this, QMessageBox::Information, tr("Инфо"),
				   tr("В текущем TIM нет изображений."));
		return;
	}

	const bool singleImage = (items.size() == 1);

	int saved = 0;
	QStringList errors;

	for (const Item &it : items) {
		Tim::Group &g = *it.g;
		int pr = qBound(0, m_paletteRow, qMax(0, int(g.clutHeader.height) - 1));
		QImage img = Tim::decodeImage(g, it.imgIdx, pr, -1);
		if (img.isNull()) {
			errors << QString("img#%1: null image").arg(it.globalIdx);
			continue;
		}

		QString name = singleImage
			? (stem + ext)
			: QString("%1_img%2%3").arg(stem).arg(it.globalIdx, 2, 10, QChar('0')).arg(ext);
		QString fullPath = dir.filePath(name);
		if (!img.save(fullPath)) {
			errors << QString("img#%1: save failed (%2)").arg(it.globalIdx).arg(fullPath);
			continue;
		}
		++saved;

		QString baseNoext = dir.filePath(singleImage
			? stem
			: QString("%1_img%2").arg(stem).arg(it.globalIdx, 2, 10, QChar('0')));
		autoSavePalettesHelper(g, baseNoext, m_autoSaveAct, m_autoSavePal, m_autoSaveGpl);

		QString namePrefix = singleImage
			? stem
			: QString("%1_img%2").arg(stem).arg(it.globalIdx, 2, 10, QChar('0'));
		saved += saveExtraPalettes(g, it.imgIdx, pr, dir.path(), namePrefix, ext);
	}

	if (m_autoSaveTtptt) {
		QString ttpttPath = dir.filePath(stem + ".ttptt");
		if (writeTtpttFile(ttpttPath, m_groups, m_sourcePath, m_paletteRow,
						   m_currentEntry, m_zoomFit, m_showStp))
			qInfo() << "Метаданные сохранены рядом с изображениями:" << ttpttPath;
		else
			qWarning() << "Не удалось сохранить метаданные:" << ttpttPath;
	}

	QString msg = QString("Сохранено %1 изображений (%2) в %3")
		.arg(saved).arg(ext).arg(dirPath);
	if (!errors.isEmpty())
		msg += "\n\nОшибки:\n" + errors.mid(0, 10).join("\n");
	showMsgBox(this, QMessageBox::Information, tr("Готово"), msg);
	setStatus(msg.split('\n').first());
	qInfo() << msg;
}

void MainWindow::openTtptt() {
	if (m_batchRunning) return;
	QString p = QFileDialog::getOpenFileName(this, "Метаданные ttptt", initialDir(),
		"Метаданные ttptt (*.ttptt);;All (*.*)");
	if (p.isEmpty()) return;
	loadTtpttFromPath(p);
}

bool MainWindow::writeTtpttToPath(const QString &path) {
	if (!writeTtpttFile(path, m_groups, m_sourcePath, m_paletteRow,
						m_currentEntry, m_zoomFit, m_showStp)) {
		showMsgBox(this, QMessageBox::Critical, "Ошибка",
				   "Не удалось сохранить метаданные:\n" + path);
		return false;
	}
	m_ttpttPath = path;
	rememberDir(path);
	updateTitle();
	setStatus("Метаданные сохранены: " + path);
	qInfo() << "Метаданные сохранены:" << path;
	return true;
}

void MainWindow::saveTtptt() {
	if (m_batchRunning || m_groups.isEmpty()) return;

	QString path = m_ttpttPath;
	if (path.isEmpty() && !m_sourcePath.isEmpty()) {
		QFileInfo fi(m_sourcePath);
		path = fi.absolutePath() + "/" + fi.completeBaseName() + ".ttptt";
	}
	if (path.isEmpty()) { saveTtpttAs(); return; }
	writeTtpttToPath(path);
}

void MainWindow::saveTtpttAs() {
	if (m_batchRunning || m_groups.isEmpty()) return;
	QString def = (m_sourceStem.isEmpty() ? "untitled" : m_sourceStem) + ".ttptt";
	QString path = QFileDialog::getSaveFileName(this, "Сохранить метаданные ttptt", def,
												"Метаданные ttptt (*.ttptt)");
	if (path.isEmpty()) return;
	writeTtpttToPath(path);
}

bool MainWindow::loadTtpttFromPath(const QString &path, bool silent) {
	if (m_batchRunning) {
		if (!silent)
			showMsgBox(this, QMessageBox::Information, tr("Инфо"), tr("Идет пакетная операция."));
		return false;
	}
	if (!silent && !confirmDiscard()) return false;

	qInfo() << "Открытие метаданных .ttptt:" << path;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		if (!silent) showMsgBox(this, QMessageBox::Critical, "Ошибка", "Не могу открыть файл");
		return false;
	}
	QByteArray raw = f.readAll();
	f.close();

	QJsonParseError perr;
	QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
	if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
		qWarning() << "Ошибка разбора .ttptt:" << perr.errorString();
		if (!silent)
			showMsgBox(this, QMessageBox::Critical, "Ошибка",
					   "Файл не является метаданными ttptt или повреждён:\n" + perr.errorString());
		return false;
	}
	QJsonObject root = doc.object();
	if (!root.contains("groups") || !root["groups"].isArray()) {
		if (!silent)
			showMsgBox(this, QMessageBox::Critical, "Ошибка", "В файле нет данных групп");
		return false;
	}

	QVector<Tim::Group> groups;
	for (const QJsonValue &v : root["groups"].toArray())
		groups.append(Tim::groupFromJson(v.toObject()));

	if (groups.isEmpty()) {
		if (!silent)
			showMsgBox(this, QMessageBox::Critical, "Ошибка", "Файл не содержит групп TIM");
		return false;
	}

	m_groups = groups;
	m_entries.clear();
	if (!m_groups.isEmpty() && !m_groups.first().prefix.isEmpty())
		qInfo() << QString("Проект содержит префикс перед первым TIM: %1 байт")
					   .arg(m_groups.first().prefix.size());
	for (int gi = 0; gi < m_groups.size(); ++gi) {
		if (m_groups[gi].images.isEmpty()) m_entries.append({ gi, -1 });
		else for (int ii = 0; ii < m_groups[gi].images.size(); ++ii)
			m_entries.append({ gi, ii });
	}

	int curEntry = root["currentEntry"].toInt(0);
	m_currentEntry = m_entries.isEmpty() ? -1 : qBound(0, curEntry, m_entries.size() - 1);

	m_paletteRow = qMax(0, root["paletteRow"].toInt(0));
	if (root.contains("zoomFit")) m_zoomFit = root["zoomFit"].toBool();
	if (root.contains("showStp")) m_showStp = root["showStp"].toBool();

	QString sourcePath = root["sourcePath"].toString();

	m_sourcePath.clear();
	m_ttpttPath = path;
	m_sourceStem = QFileInfo(path).completeBaseName();
	m_dirty = false;

	// Подхват PNG или заглушек
	QStringList importedPngs =
		importPngsForTtptt(QFileInfo(path).absolutePath(), m_sourceStem);

	rememberDir(path);
	addRecentFile(path);
	refreshAll();
	updateTitle();

	QString statusMsg = QString("Метаданные: %1 | групп: %2 | изобр.: %3")
		.arg(QFileInfo(path).fileName()).arg(m_groups.size()).arg(m_entries.size());
	if (!importedPngs.isEmpty())
		statusMsg += QString(" | PNG: %1").arg(importedPngs.size());
	if (!sourcePath.isEmpty())
		statusMsg += QString(" | исходник: %1").arg(QFileInfo(sourcePath).fileName());
	setStatus(statusMsg);
	qInfo() << statusMsg;
	if (!importedPngs.isEmpty())
		qInfo() << "Подхвачены PNG:" << importedPngs.join(", ");
	return true;
}

QStringList MainWindow::importPngsForTtptt(const QString &dirPath, const QString &stem) {
	QStringList imported;
	if (m_groups.isEmpty() || stem.isEmpty()) return imported;

	struct Item { Tim::Group *g; int imgIdx; int globalIdx; };
	QVector<Item> items;
	int n = 0;
	for (int gi = 0; gi < m_groups.size(); ++gi)
		for (int ii = 0; ii < m_groups[gi].images.size(); ++ii) {
			++n;
			items.append({ &m_groups[gi], ii, n });
		}
	if (items.isEmpty()) return imported;

	const bool singleImage = (items.size() == 1);

	for (const Item &it : items) {
		Tim::Group &g = *it.g;

		const int w = Tim::widthWordsToPixels(g.bpp, g.images[it.imgIdx].header.widthWords);
		const int h = g.images[it.imgIdx].header.height;
		if (w <= 0 || h <= 0) continue;

		const QString pngPath = findTimPng(dirPath, stem, it.globalIdx, singleImage, true);

		QImage im;
		if (!pngPath.isEmpty()) {
			im = QImage(pngPath);
			if (!im.isNull()) imported << QFileInfo(pngPath).fileName();
		}
		if (im.isNull()) {
			const QString preferredName = singleImage
				? (stem + ".png")
				: QString("%1_img%2.png").arg(stem).arg(it.globalIdx, 2, 10, QChar('0'));
			im = PlaceholderArt::makeMissingTimPng(w, h, it.globalIdx, g.bpp, preferredName);
			qWarning() << "Нет PNG для image #" << it.globalIdx
					   << "- вставлена заглушка" << w << "x" << h;
		}

		im = Tim::normalizeImageForTim(im, w, h);

		const int palRow = qBound(0, m_paletteRow, qMax(0, int(g.clutHeader.height) - 1));
		if (!Tim::applyEncodedToImage(g, it.imgIdx, im, palRow))
			qWarning() << "Не удалось закодировать PNG/заглушку для img#" << it.imgIdx;
	}
	return imported;
}

void MainWindow::closeFile() {
	if (m_batchRunning || !confirmDiscard()) return;
	qInfo() << "Закрытие файла:"
			<< (!m_sourcePath.isEmpty() ? QFileInfo(m_sourcePath).fileName()
				: (!m_ttpttPath.isEmpty() ? QFileInfo(m_ttpttPath).fileName()
										  : "(без имени)"));
	m_groups.clear();
	m_entries.clear();
	m_currentEntry = -1;
	m_sourcePath.clear();
	m_ttpttPath.clear();
	m_sourceStem.clear();
	m_paletteRow = 0;
	m_dirty = false;
	refreshAll();
	updateTitle();
	setStatus("Закрыто.");
}

void MainWindow::prevImage() { if (m_currentEntry > 0) { --m_currentEntry; refreshAll(); } }
void MainWindow::nextImage() { if (m_currentEntry < m_entries.size() - 1) { ++m_currentEntry; refreshAll(); } }
void MainWindow::prevPaletteRow() { Tim::Group *g = currentGroup(); if (g && g->hasClut && m_paletteRow > 0) { --m_paletteRow; refreshAll(); } }
void MainWindow::nextPaletteRow() { Tim::Group *g = currentGroup(); if (g && g->hasClut && m_paletteRow < qMax(1, int(g->clutHeader.height)) - 1) { ++m_paletteRow; refreshAll(); } }
void MainWindow::previewScale1to1() { m_zoomFit = false; refreshPreview(); }
void MainWindow::previewScaleFit() { m_zoomFit = true; refreshPreview(); }
void MainWindow::toggleStp() { m_showStp = !m_showStp; if (m_stpAction) m_stpAction->setChecked(m_showStp); qInfo() << "Прозрачность (STP):" << (m_showStp ? "вкл" : "выкл"); refreshPreview(); }

void MainWindow::toggleLogWindow() {
	if (!m_logWindow) return;
	m_showLog = !m_logWindow->isVisible();
	if (m_showLog) {
		if (m_logWindow->pos().isNull()) {
			m_logWindow->move(frameGeometry().center() - m_logWindow->rect().center());
		}
		m_logWindow->show();
	} else {
		m_logWindow->hide();
	}
	if (m_logAction) m_logAction->setChecked(m_showLog);
}

void MainWindow::applyMetadata() {
	if (m_batchRunning) return;
	int ii = -1; Tim::Group *g = currentGroup(&ii); if (!g) return;
	int bpp = m_bppCombo->currentIndex(); if (bpp < 0 || bpp > 3) return;
	g->flags = quint32(bpp) | (m_hasClutCheck->isChecked() ? 0x8 : 0) | (m_hasImageCheck->isChecked() ? 0x10 : 0);
	g->bpp = quint8(bpp); g->hasClut = m_hasClutCheck->isChecked(); g->hasImage = m_hasImageCheck->isChecked();
	auto parseInto = [&](QLineEdit *e, quint16 *out) { int v; if (parseFlexible(e->text(), &v)) *out = quint16(v); };
	if (g->hasClut) { parseInto(m_clutXEdit, &g->clutHeader.x); parseInto(m_clutYEdit, &g->clutHeader.y); parseInto(m_clutWidthEdit, &g->clutHeader.widthWords); parseInto(m_clutHeightEdit, &g->clutHeader.height); g->clutHeader.size = 12 + quint32(g->clutHeader.widthWords) * g->clutHeader.height * 2; }
	if (ii >= 0 && ii < g->images.size()) { auto &h = g->images[ii].header; parseInto(m_imgXEdit, &h.x); parseInto(m_imgYEdit, &h.y); parseInto(m_imgWidthEdit, &h.widthWords); parseInto(m_imgHeightEdit, &h.height); h.size = 12 + quint32(h.widthWords) * h.height * 2; }
	m_dirty = true; rebuildAndReparse(true); updateTitle(); setStatus("Применено.");
	qInfo() << QString("Метаданные применены (bpp=%1, flags=0x%2)").arg(bpp).arg(g->flags & 0xFF, 2, 16, QChar('0'));
}

void MainWindow::resetMetadata() {
	if (m_sourcePath.isEmpty()) return;
	qInfo() << "Сброс метаданных — перечитывание" << QFileInfo(m_sourcePath).fileName();
	loadTimFromPath(m_sourcePath, true);
	refreshAll(); setStatus("Сброшено.");
}
void MainWindow::convertTo16bpp() { convertBpp(2); }
void MainWindow::convertTo24bpp() { convertBpp(3); }

void MainWindow::convertBpp(int targetBpp) {
	if (m_batchRunning) return;
	int ii = -1; Tim::Group *g = currentGroup(&ii); if (!g || ii < 0) return;
	int currentBpp = g->bpp; if (currentBpp == targetBpp || (currentBpp != 2 && currentBpp != 3) || (targetBpp != 2 && targetBpp != 3)) return;
	int oldW = Tim::widthWordsToPixels(currentBpp, g->images[ii].header.widthWords);
	QImage img = Tim::decodeImage(*g, ii, m_paletteRow, -1); if (img.isNull()) return;
	g->bpp = quint8(targetBpp); g->flags = (g->flags & ~0x7u) | quint32(targetBpp);
	g->images[ii].header.widthWords = quint16((targetBpp == 2) ? oldW : Tim::pixelsToWords(targetBpp, oldW));
	QByteArray newData = Tim::encodeImage(img, *g, g->images[ii].header, m_paletteRow);
	if (newData.isEmpty()) { g->bpp = quint8(currentBpp); g->flags = (g->flags & ~0x7u) | quint32(currentBpp); return; }
	g->images[ii].data = newData; g->images[ii].header.size = 12 + newData.size();
	rebuildAndReparse(true); m_dirty = true; updateTitle();
	qInfo() << QString("Конвертация bpp: %1bpp → %2bpp (%3x%4)")
		.arg(currentBpp * 8).arg(targetBpp * 8).arg(oldW).arg(g->images[ii].header.height);
}

void MainWindow::splitImage() {
	if (m_batchRunning) return;
	int ii = -1; Tim::Group *g = currentGroup(&ii); if (!g || ii < 0) return;
	int bpp = g->bpp; int width = Tim::widthWordsToPixels(bpp, g->images[ii].header.widthWords); int height = g->images[ii].header.height;
	if (width < 2 || width % 2 != 0) return;
	int halfW = width / 2; int halfWW = Tim::pixelsToWords(bpp, halfW);
	QImage full = Tim::decodeImage(*g, ii, m_paletteRow, -1); if (full.isNull()) return;
	QImage left = full.copy(0, 0, halfW, height); QImage right = full.copy(halfW, 0, halfW, height);
	Tim::Image imL, imR;
	imL.header = g->images[ii].header; imL.header.widthWords = quint16(halfWW);
	imL.data = Tim::encodeImage(left, *g, imL.header, m_paletteRow); imL.header.size = 12 + imL.data.size();
	imR.header = g->images[ii].header; imR.header.widthWords = quint16(halfWW);
	imR.data = Tim::encodeImage(right, *g, imR.header, m_paletteRow); imR.header.size = 12 + imR.data.size();
	g->images[ii] = imL; g->images.insert(ii + 1, imR);
	rebuildAndReparse(true); m_dirty = true; updateTitle();
	qInfo() << QString("Изображение разделено: %1x%2 → два %3x%4")
		.arg(width).arg(height).arg(halfW).arg(height);
}

void MainWindow::mergeWithNext() { mergeImages(true); }
void MainWindow::mergeWithPrev() { mergeImages(false); }

void MainWindow::mergeImages(bool withNext) {
	if (m_batchRunning) return;
	int ii = -1; Tim::Group *g = currentGroup(&ii); if (!g || ii < 0) return;
	int idx1 = withNext ? ii : ii - 1; int idx2 = withNext ? ii + 1 : ii;
	if (idx1 < 0 || idx2 >= g->images.size()) return;
	int bpp = g->bpp; int w1 = Tim::widthWordsToPixels(bpp, g->images[idx1].header.widthWords);
	int w2 = Tim::widthWordsToPixels(bpp, g->images[idx2].header.widthWords);
	int h1 = g->images[idx1].header.height; if (h1 != g->images[idx2].header.height) return;
	int totalW = w1 + w2; int newWW = Tim::pixelsToWords(bpp, totalW);
	QImage i1 = Tim::decodeImage(*g, idx1, m_paletteRow, -1); QImage i2 = Tim::decodeImage(*g, idx2, m_paletteRow, -1);
	if (i1.isNull() || i2.isNull()) return;
	QImage merged(totalW, h1, QImage::Format_ARGB32); merged.fill(Qt::transparent);
	QPainter p(&merged); p.drawImage(0, 0, i1); p.drawImage(w1, 0, i2); p.end();
	Tim::Image mergedIm; mergedIm.header = g->images[idx1].header; mergedIm.header.widthWords = quint16(newWW);
	mergedIm.data = Tim::encodeImage(merged, *g, mergedIm.header, m_paletteRow); mergedIm.header.size = 12 + mergedIm.data.size();
	g->images.removeAt(idx2); g->images.removeAt(idx1); g->images.insert(idx1, mergedIm);
	rebuildAndReparse(true); m_dirty = true; updateTitle();
	qInfo() << QString("Изображения слиты: %1x%2 + %3x%4 → %5x%6")
		.arg(w1).arg(h1).arg(w2).arg(h1).arg(totalW).arg(h1);
}

void MainWindow::rebuildAndReparse(bool preserveEntry) {
	int old = preserveEntry ? m_currentEntry : 0;
	QByteArray data = Tim::rebuildAll(m_groups); m_groups = Tim::parseAll(data); m_entries.clear();
	for (int gi = 0; gi < m_groups.size(); ++gi) {
		if (m_groups[gi].images.isEmpty()) m_entries.append({gi, -1});
		else for (int j = 0; j < m_groups[gi].images.size(); ++j) m_entries.append({gi, j});
	}
	m_currentEntry = m_entries.isEmpty() ? -1 : qBound(0, old, m_entries.size() - 1); refreshAll();
}

void MainWindow::exportPalette() {
	Tim::Group *g = currentGroup();
	if (!g || g->clut.isEmpty()) return;

	int cw = 0, ch = 0;
	QVector<QColor> flat = Tim::safeClutFlat(*g, &cw, &ch);
	if (flat.isEmpty()) return;

	QString path = QFileDialog::getSaveFileName(
		this, "Pal",
		m_sourceStem + "_palette",
		"ACT (*.act);;PAL (*.pal);;GPL (*.gpl);;PNG (*.png);;BMP (*.bmp)");
	if (path.isEmpty()) return;

	QString err;
	if (!Palettes::saveAny(path, flat, cw, ch, &err)) {
		qWarning() << "Экспорт палитры не удался:" << path << err;
		showMsgBox(this, QMessageBox::Critical, "Ошибка",
				   "Не удалось сохранить палитру:\n" + err);
		return;
	}
	rememberDir(path);
	qInfo() << QString("Экспорт палитры: %1 (%2 цветов, %3x%4)")
				   .arg(path).arg(flat.size()).arg(cw).arg(ch);
	setStatus("Палитра сохранена: " + path);
}

void MainWindow::importPalette() {
	Tim::Group *g = currentGroup();
	if (!g || !g->hasClut) return;

	QString path = QFileDialog::getOpenFileName(
		this, "Pal", initialDir(),
		"Pals (*.pal *.act *.gpl *.png *.bmp)");
	if (path.isEmpty()) return;

	importPaletteFromPath(path);
}

bool MainWindow::importPaletteFromPath(const QString &path) {
	Tim::Group *g = currentGroup(); if (!g || !g->hasClut) return false;
	QString err; Palettes::Loaded l = Palettes::loadAny(path, &err); if (!l.valid) return false;
	int target = int(g->clutHeader.widthWords) * qMax<int>(1, g->clutHeader.height);
	QVector<QColor> pal = l.colors;
	if (pal.size() > target) pal = pal.mid(0, target);
	else while (pal.size() < target) pal.append(QColor(0, 0, 0));
	QByteArray clut(target * 2, '\0'); uchar *p = reinterpret_cast<uchar*>(clut.data());
	for (int i = 0; i < target; ++i) Tim::wr16(p + i * 2, Tim::rgbToBgr555(pal[i].red(), pal[i].green(), pal[i].blue()));
	g->clut = clut; g->clutHeader.size = 12 + clut.size(); m_dirty = true; rebuildAndReparse(true); rememberDir(path);
	qInfo() << QString("Импортирована палитра из %1 (%2 цветов)").arg(QFileInfo(path).fileName()).arg(target);
	return true;
}

void MainWindow::replaceImage() {
	if (m_batchRunning) return;
	int ii = -1; Tim::Group *g = currentGroup(&ii); if (!g || ii < 0) return;
	int w = Tim::widthWordsToPixels(g->bpp, g->images[ii].header.widthWords); int h = g->images[ii].header.height;
	QString path = QFileDialog::getOpenFileName(this, QString("Img (%1x%2)").arg(w).arg(h), initialDir(), "Img (*.png *.bmp)");
	if (!path.isEmpty()) replaceImageFromPath(path);
}

bool MainWindow::replaceImageFromPath(const QString &path) {
	int ii = -1; Tim::Group *g = currentGroup(&ii); if (!g || ii < 0) return false;
	QImage img(path); if (img.isNull()) return false;

	const int bpp = g->bpp;
	auto &h = g->images[ii].header;
	const int oldWW = h.widthWords;
	const int oldH  = h.height;
	const int oldW  = Tim::widthWordsToPixels(bpp, oldWW);

	const int newWW	= Tim::pixelsToWords(bpp, img.width());
	const int actualW  = Tim::widthWordsToPixels(bpp, newWW);
	const int newH	 = img.height();
	const bool sizeChanged = (actualW != oldW || newH != oldH);
	const bool padNeeded   = (img.width() != actualW);

	qInfo() << "Замена изображения из" << QFileInfo(path).fileName();

	if (sizeChanged || padNeeded) {
		QString msg = QString("TIM заголовок изображения будет обновлён по файлу:\n"
							  "  png файл: %1x%2\n"
							  "  было: width_words=%3, height=%4 (%5x%6)\n"
							  "  станет: width_words=%7, height=%8 (%9x%10)")
			.arg(img.width()).arg(img.height())
			.arg(oldWW).arg(oldH).arg(oldW).arg(oldH)
			.arg(newWW).arg(newH).arg(actualW).arg(newH);
		if (padNeeded) {
			msg += QString("\n\nШирина будет округлена до %1 пикс. (%2bpp, word boundary).")
				.arg(actualW).arg(bpp * 8);
		}
		if (showMsgBox(this, QMessageBox::Question, "Замена изображения", msg,
					   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
			qInfo() << "Замена изображения отменена пользователем";
			return false;
		}
	}

	img = Tim::normalizeImageForTim(img, actualW, newH);

	h.widthWords = quint16(newWW);
	h.height	 = quint16(newH);

	if (!Tim::applyEncodedToImage(*g, ii, img, m_paletteRow)) return false;

	rememberDir(path);
	rebuildAndReparse(true);
	m_dirty = true;
	updateTitle();

	qInfo() << QString("Изображение заменено: %1x%2 → %3x%4 (ww=%5, h=%6, %7 байт)")
		.arg(oldW).arg(oldH).arg(actualW).arg(newH).arg(newWW).arg(newH)
		.arg(g->images[ii].data.size());
	setStatus(QString("Изображение заменено: %1x%2 (было %3x%4)")
		.arg(actualW).arg(newH).arg(oldW).arg(oldH));
	return true;
}

// =====================================================================
// BATCH PROCESSING (Threading)
// =====================================================================
void MainWindow::batchExtract() {
	if (m_batchRunning) {
		showMsgBox(this, QMessageBox::Information, "Инфо", "Уже выполняется.");
		return;
	}
	QString folder = QFileDialog::getExistingDirectory(this, "Папка", initialDir());
	if (folder.isEmpty()) return;
	QDir dir(folder);
	QStringList filesInDir = dir.entryList(QDir::Files);
	if (filesInDir.isEmpty()) {
		showMsgBox(this, QMessageBox::Information, "Инфо", "Пусто.");
		return;
	}

	// Формат
	QStringList fmtItems;
	fmtItems << tr("PNG (*.png)") << tr("BMP (*.bmp)");
	bool ok = false;
	QString fmt = QInputDialog::getItem(this, tr("Формат изображений"),
		tr("Формат для извлечённых изображений:"), fmtItems,
		m_batchImageFormat == "bmp" ? 1 : 0, false, &ok);
	if (!ok) return;
	m_batchImageFormat = fmt.contains("bmp", Qt::CaseInsensitive) ? "bmp" : "png";
	const QString imageExt = "." + m_batchImageFormat;

	qInfo() << QString("Пакетное извлечение: папка %1 (%2 файлов), формат %3")
		.arg(folder).arg(filesInDir.size()).arg(m_batchImageFormat);
	m_batchRunning = true;
	setStatus("Извлечение...");
	rememberDir(folder + "/x");

	m_batchThread = new QThread(this);
	BatchWorker *worker = new BatchWorker();
	worker->moveToThread(m_batchThread);

	bool anyPal = m_batchSaveAct || m_batchSavePal || m_batchSaveGpl;
	bool saveMeta = m_batchSaveTtptt;

	connect(m_batchThread, &QThread::started, worker, [=]() {
		worker->runExtract(folder, anyPal, m_batchSaveAct, m_batchSavePal,
						   m_batchSaveGpl, m_saveAllPalettesBatch, saveMeta,
						   imageExt);
	});
	connect(worker, &BatchWorker::progress, this, &MainWindow::onBatchProgress);
	connect(worker, &BatchWorker::logMsg, this, &MainWindow::onBatchLog);
	connect(worker, &BatchWorker::finished, this, &MainWindow::onBatchFinished);
	connect(m_batchThread, &QThread::finished, worker, &QObject::deleteLater);
	connect(m_batchThread, &QThread::finished, m_batchThread, &QObject::deleteLater);
	connect(m_batchThread, &QThread::finished, this, [this]() { m_batchThread = nullptr; });
	m_batchThread->start();
}

void MainWindow::batchReplace() {
	if (m_batchRunning) { showMsgBox(this, QMessageBox::Information, "Инфо", "Уже выполняется."); return; }
	QString folder = QFileDialog::getExistingDirectory(this, "Папка", initialDir()); if (folder.isEmpty()) return;
	QDir dir(folder); QStringList filesInDir = dir.entryList(QDir::Files);
	if (filesInDir.isEmpty()) { showMsgBox(this, QMessageBox::Information, "Инфо", "Пусто."); return; }
	if (showMsgBox(this, QMessageBox::Question, "Замена", "Перезаписать TIM?",
				   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) return;
	qInfo() << QString("Пакетная замена: папка %1 (%2 файлов)").arg(folder).arg(filesInDir.size());
	m_batchRunning = true; setStatus("Замена..."); rememberDir(folder + "/x");
	QString currentSource = m_sourcePath;
	m_batchThread = new QThread(this); BatchWorker *worker = new BatchWorker(); worker->moveToThread(m_batchThread);
	connect(m_batchThread, &QThread::started, worker, [=]() { worker->runReplace(folder, currentSource); });
	connect(worker, &BatchWorker::progress, this, &MainWindow::onBatchProgress);
	connect(worker, &BatchWorker::logMsg, this, &MainWindow::onBatchLog);
	connect(worker, &BatchWorker::finished, this, &MainWindow::onBatchFinished);
	connect(m_batchThread, &QThread::finished, worker, &QObject::deleteLater);
	connect(m_batchThread, &QThread::finished, m_batchThread, &QObject::deleteLater);
	connect(m_batchThread, &QThread::finished, this, [this]() { m_batchThread = nullptr; });
	m_batchThread->start();
}

void MainWindow::onBatchProgress(int current, int total, const QString &fileName) { setStatus(tr("%1/%2 — %3").arg(current).arg(total).arg(fileName)); }
void MainWindow::onBatchLog(const QString &msg) { qInfo() << msg; }
void MainWindow::onBatchFinished(const QString &summary, const QStringList &errors,
								 const QStringList &warnings, bool openedFileRewritten) {
	m_batchRunning = false;
	if (openedFileRewritten && !m_sourcePath.isEmpty()) loadTimFromPath(m_sourcePath, true);
	QString msg = summary;
	if (!errors.isEmpty()) msg += "\n\nОшибки:\n" + errors.mid(0, 10).join("\n");
	if (!warnings.isEmpty()) {
		msg += QString("\n\nУведомления (PNG-размеры не совпали с TIM): %1\n%2")
			.arg(warnings.size())
			.arg(warnings.mid(0, 30).join("\n"));
		if (warnings.size() > 30) msg += QString("\n...и ещё %1").arg(warnings.size() - 30);
		for (const auto &w : warnings) qWarning() << "size mismatch:" << w;
	}
	qInfo() << summary;
	showMsgBox(this, QMessageBox::Information, "Готово", msg);
	setStatus(summary);
}

// =====================================================================
// UI / Display
// =====================================================================
void MainWindow::refreshAll() { refreshPalette(); refreshPreview(); refreshParams(); updateNavButtons(); }

void MainWindow::refreshPalette() {
	m_palCells.clear();
	Tim::Group *g = currentGroup();
	if (!g || !g->hasClut || g->clut.isEmpty()) {
		m_paletteLabel->setPixmap(QPixmap());
		if (m_palInfoLabel) m_palInfoLabel->setText("");
		return;
	}
	int cw = g->clutHeader.widthWords;
	int ch = qMax<int>(1, g->clutHeader.height);
	m_paletteRow = qBound(0, m_paletteRow, ch - 1);
	QVector<QRgb> pal = Tim::getClutPalette(*g, m_paletteRow);
	if (pal.isEmpty()) { m_paletteLabel->setPixmap(QPixmap()); return; }

	const int n	  = pal.size();
	const int totalW = qMax(1, m_paletteLabel->width());
	const int cellH  = qMax(1, m_paletteLabel->height());

	QImage img(totalW, cellH, QImage::Format_RGB32);

	for (int i = 0; i < n; ++i) {
		const int x0 = (i * totalW) / n;
		const int x1 = ((i + 1) * totalW) / n;

		const QRgb color = pal[i];
		for (int y = 0; y < cellH; ++y) {
			QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
			for (int x = x0; x < x1; ++x) line[x] = color;
		}

		PalCell c; c.x0 = x0; c.x1 = x1; c.idx = i;
		c.r = qRed(color); c.g = qGreen(color); c.b = qBlue(color);
		const int off = (m_paletteRow * cw + i) * 2;
		c.raw = (off + 2 <= g->clut.size())
			? Tim::rd16(reinterpret_cast<const uchar*>(g->clut.constData() + off))
			: 0;
		m_palCells.append(c);
	}
	m_paletteLabel->setPixmap(QPixmap::fromImage(img));
	if (m_palInfoLabel)
		m_palInfoLabel->setText(QString("%1 стр. / %2 цветов").arg(ch).arg(n));
}

void MainWindow::onPreviewClick(const QPoint &labelPos) {
	int ii = -1;
	Tim::Group *g = currentGroup(&ii);
	if (!g || ii < 0) return;
	if (m_previewSrcSize.isEmpty() || m_previewDisplaySize.isEmpty()) return;

	QSize lblSz = m_previewLabel->size();
	int ox = (lblSz.width()  - m_previewDisplaySize.width())  / 2;
	int oy = (lblSz.height() - m_previewDisplaySize.height()) / 2;

	int dx = labelPos.x() - ox;
	int dy = labelPos.y() - oy;
	if (dx < 0 || dy < 0 ||
		dx >= m_previewDisplaySize.width() ||
		dy >= m_previewDisplaySize.height()) {
		return;
	}

	int sx = dx * m_previewSrcSize.width()  / qMax(1, m_previewDisplaySize.width());
	int sy = dy * m_previewSrcSize.height() / qMax(1, m_previewDisplaySize.height());
	if (sx < 0 || sy < 0 ||
		sx >= m_previewSrcSize.width() ||
		sy >= m_previewSrcSize.height()) return;

	int bpp = g->bpp;
	const Tim::Image &im = g->images[ii];
	const QByteArray &data = im.data;
	int ww = im.header.widthWords;
	int width = Tim::widthWordsToPixels(bpp, ww);

	QString msg;

	if (bpp == 0 || bpp == 1) {
		int idx = 0;
		if (bpp == 0) {
			int byteIdx = (sy * width + sx) / 2;
			if (byteIdx < data.size()) {
				uchar byte = uchar(data[byteIdx]);
				idx = ((sx & 1) == 0) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
			}
		} else {
			int byteIdx = sy * width + sx;
			if (byteIdx < data.size()) idx = uchar(data[byteIdx]);
		}
		int cw = g->clutHeader.widthWords;
		int off = (m_paletteRow * cw + idx) * 2;
		if (off + 2 <= g->clut.size()) {
			quint16 raw = Tim::rd16(reinterpret_cast<const uchar*>(g->clut.constData() + off));
			QColor c = QColor::fromRgb(Tim::bgr555ToRgb(raw));
			bool stp = (raw & 0x8000) != 0;
			QString hex = QString("0x%1").arg(raw, 4, 16, QChar('0')).toUpper();
			msg = QString("px: (%1,%2) ИНДЕКС[%3]: RGB=(%4,%5,%6) BGR555=%7%8")
				.arg(sx).arg(sy).arg(idx)
				.arg(c.red()).arg(c.green()).arg(c.blue())
				.arg(hex)
				.arg(stp ? " [STP]" : "");
		} else {
			msg = QString("px (%1,%2) idx %3").arg(sx).arg(sy).arg(idx);
		}
	} else if (bpp == 2) {
		int pxi = sy * width + sx;
		quint16 c16 = 0;
		if (pxi * 2 + 2 <= data.size()) {
			c16 = Tim::rd16(reinterpret_cast<const uchar*>(data.constData() + pxi * 2));
		}
		int r = c16 & 0x1F, gg = (c16 >> 5) & 0x1F, b = (c16 >> 10) & 0x1F;
		int rr = (r << 3) | (r >> 2);
		int gv = (gg << 3) | (gg >> 2);
		int bv = (b << 3) | (b >> 2);
		bool stp = (c16 & 0x8000) != 0;
		QString hex = QString("0x%1").arg(c16, 4, 16, QChar('0')).toUpper();
		msg = QString("px (%1,%2): RGB=(%3,%4,%5) BGR555=%6%7")
			.arg(sx).arg(sy).arg(rr).arg(gv).arg(bv)
			.arg(hex)
			.arg(stp ? " [STP]" : "");
	} else if (bpp == 3) {
		int stride = ww * 2;
		int pos = sy * stride + sx * 3;
		if (pos + 2 < data.size()) {
			const uchar *p = reinterpret_cast<const uchar*>(data.constData() + pos);
			msg = QString("px (%1,%2): RGB=(%3,%4,%5)")
				.arg(sx).arg(sy).arg(p[0]).arg(p[1]).arg(p[2]);
		} else {
			msg = QString("px (%1,%2)").arg(sx).arg(sy);
		}
	}

	if (!msg.isEmpty()) setStatus(msg);
}

void MainWindow::onPaletteClick(int x) {
	for (const auto &c : m_palCells) {
		if (x >= c.x0 && x < c.x1) {
			setStatus(QString("ИНДЕКС[%1]: RGB=(%2,%3,%4) BGR555=0x%5").arg(c.idx).arg(c.r).arg(c.g).arg(c.b).arg(c.raw, 4, 16, QChar('0')).toUpper());
			return;
		}
	}
}

void MainWindow::editPaletteColor(int idx) {
	Tim::Group *g = currentGroup();
	if (!g || !g->hasClut || g->clut.isEmpty()) return;
	int cw = g->clutHeader.widthWords;
	int ch = qMax<int>(1, g->clutHeader.height);
	if (idx < 0 || idx >= cw) return;

	int off = (m_paletteRow * cw + idx) * 2;
	if (off + 2 > g->clut.size()) return;

	quint16 raw = Tim::rd16(reinterpret_cast<const uchar*>(g->clut.constData() + off));
	QColor current = QColor::fromRgb(Tim::bgr555ToRgb(raw));
	bool stp = (raw & 0x8000) != 0;

	QColor chosen = QColorDialog::getColor(
		current, this,
		tr("ИНДЕКС [%1] — палитра %2").arg(idx).arg(m_paletteRow));
	if (!chosen.isValid()) return;   // отмена

	quint16 newRaw = Tim::rgbToBgr555(
		chosen.red(), chosen.green(), chosen.blue(), stp ? 1 : 0);

	uchar *p = reinterpret_cast<uchar*>(g->clut.data() + off);
	Tim::wr16(p, newRaw);

	m_dirty = true;
	updateTitle();
	refreshPalette();
	refreshPreview();
	setStatus(QString("ИНДЕКС[%1] изменён: RGB=(%2,%3,%4)")
		.arg(idx).arg(chosen.red()).arg(chosen.green()).arg(chosen.blue()));
	qInfo() << QString("Палитра %1, индекс %2 → RGB(%3,%4,%5)")
		.arg(m_paletteRow).arg(idx)
		.arg(chosen.red()).arg(chosen.green()).arg(chosen.blue());
}

void MainWindow::refreshPreview() {
	if (!m_previewLabel) return;
	m_previewSrcSize	 = QSize();
	m_previewDisplaySize = QSize();

	int ii = -1;
	Tim::Group *g = currentGroup(&ii);
	QImage src;
	if (g && ii >= 0)
		src = Tim::decodeImage(*g, ii, m_paletteRow, m_showStp ? 1 : 0);

	if (src.isNull()) {
		const QSize sz = m_previewLabel->size();
		if (sz.width() < 40 || sz.height() < 40) {
			m_previewLabel->setPixmap(QPixmap());
			return;
		}
		QImage img = PlaceholderArt::renderPlaceholder(
			sz, font(),
			tr("Откройте TIM"),
			tr("PS1 навсегда · ttptt"));
		m_previewLabel->setPixmap(img.isNull() ? QPixmap()
											   : QPixmap::fromImage(img));
		return;
	}

	QImage display = src;
	if (m_zoomFit) {
		QSize available = m_previewLabel->size();
		QSize target = src.size().scaled(available, Qt::KeepAspectRatio);
		if (target != src.size())
			display = src.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation);
	}

	m_previewSrcSize	 = src.size();
	m_previewDisplaySize = display.size();

	QImage board = Tim::makeCheckerboard(display.width(), display.height(), 8);
	QPainter p(&board);
	p.drawImage(0, 0, display);
	p.end();
	m_previewLabel->setPixmap(QPixmap::fromImage(board));
}

void MainWindow::refreshParams() {
	m_suppressSizeTrace = true; int ii = -1; Tim::Group *g = currentGroup(&ii);
	if (!g) {
		m_bppCombo->setCurrentIndex(-1); m_hasClutCheck->setChecked(false); m_hasImageCheck->setChecked(false); m_flagsDisplay->setText("0x00");
		for (auto *e : {m_clutSizeEdit, m_clutXEdit, m_clutYEdit, m_clutWidthEdit, m_clutHeightEdit, m_imgSizeEdit, m_imgXEdit, m_imgYEdit, m_imgWidthEdit, m_imgHeightEdit}) e->clear();
		m_imagePixelsLabel->clear(); m_suppressSizeTrace = false; return;
	}
	m_bppCombo->setCurrentIndex(g->bpp); m_hasClutCheck->setChecked(g->hasClut); m_hasImageCheck->setChecked(g->hasImage);
	m_flagsDisplay->setText(QString("0x%1").arg(g->flags & 0xFF, 2, 16, QChar('0')).toUpper());
	if (g->hasClut) { m_clutSizeEdit->setText(QString::number(g->clutHeader.size)); m_clutXEdit->setText(QString::number(g->clutHeader.x)); m_clutYEdit->setText(QString::number(g->clutHeader.y)); m_clutWidthEdit->setText(QString::number(g->clutHeader.widthWords)); m_clutHeightEdit->setText(QString::number(g->clutHeader.height)); }
	else { for (auto *e : {m_clutSizeEdit, m_clutXEdit, m_clutYEdit, m_clutWidthEdit, m_clutHeightEdit}) e->clear(); }
	if (ii >= 0 && ii < g->images.size()) {
		auto &h = g->images[ii].header; m_imgSizeEdit->setText(QString::number(h.size)); m_imgXEdit->setText(QString::number(h.x)); m_imgYEdit->setText(QString::number(h.y));
		m_imgWidthEdit->setText(QString::number(h.widthWords)); m_imgHeightEdit->setText(QString::number(h.height));
		m_imagePixelsLabel->setText(QString("%1 * %2 px").arg(Tim::widthWordsToPixels(g->bpp, h.widthWords)).arg(h.height));
	} else { for (auto *e : {m_imgSizeEdit, m_imgXEdit, m_imgYEdit, m_imgWidthEdit, m_imgHeightEdit}) e->clear(); m_imagePixelsLabel->clear(); }
	m_suppressSizeTrace = false;
}

void MainWindow::updateNavButtons() {
	int n = m_entries.size(); m_navLabel->setText(n <= 0 ? "—" : QString("%1/%2").arg(m_currentEntry + 1).arg(n));
	m_imgPrevBtn->setEnabled(m_currentEntry > 0); m_imgNextBtn->setEnabled(m_currentEntry < n - 1);
	Tim::Group *g = currentGroup(); int totalPal = (g && g->hasClut) ? qMax<int>(1, g->clutHeader.height) : 0;
	m_palRowLabel->setText(totalPal <= 0 ? "—" : QString("%1/%2").arg(m_paletteRow + 1).arg(totalPal));
	m_palPrevBtn->setEnabled(totalPal > 0 && m_paletteRow > 0); m_palNextBtn->setEnabled(totalPal > 0 && m_paletteRow < totalPal - 1);
}

void MainWindow::updateFlagsDisplay() {
	int bpp = m_bppCombo->currentIndex(); if (bpp < 0) { m_flagsDisplay->setText("?"); return; }
	quint32 flags = quint32(bpp) | (m_hasClutCheck->isChecked() ? 0x8u : 0u) | (m_hasImageCheck->isChecked() ? 0x10u : 0u);
	m_flagsDisplay->setText(QString("0x%1").arg(flags & 0xFF, 2, 16, QChar('0')).toUpper()); updateImagePixelsLabel();
}

void MainWindow::updateImagePixelsLabel() {
	if (m_suppressSizeTrace) return; int bpp = m_bppCombo->currentIndex(); if (bpp < 0) { m_imagePixelsLabel->clear(); return; }
	int ww, hh; if (!parseFlexible(m_imgWidthEdit->text(), &ww) || !parseFlexible(m_imgHeightEdit->text(), &hh) || ww <= 0 || hh <= 0) { m_imagePixelsLabel->clear(); return; }
	m_imagePixelsLabel->setText(QString("%1 * %2 px").arg(Tim::widthWordsToPixels(bpp, ww)).arg(hh));
}

void MainWindow::recomputeSizeFromDims(QLineEdit *wEdit, QLineEdit *hEdit,
									   QLineEdit *sizeEdit, bool updatePixels) {
	if (m_suppressSizeTrace) return;
	int ww, hh;
	if (!parseFlexible(wEdit->text(), &ww) || !parseFlexible(hEdit->text(), &hh) ||
		ww < 0 || hh < 0) {
		if (updatePixels) updateImagePixelsLabel();
		return;
	}
	m_suppressSizeTrace = true;
	sizeEdit->setText(QString::number(12 + ww * hh * 2));
	m_suppressSizeTrace = false;
	if (updatePixels) updateImagePixelsLabel();
}

void MainWindow::onClutDimsChanged() {
	recomputeSizeFromDims(m_clutWidthEdit, m_clutHeightEdit, m_clutSizeEdit, false);
}

void MainWindow::onImageDimsChanged() {
	recomputeSizeFromDims(m_imgWidthEdit, m_imgHeightEdit, m_imgSizeEdit, true);
}

void MainWindow::updateTitle() {
	QString title = QString("ttptt: tim-to-png-to-tim v%1")
						.arg(QCoreApplication::applicationVersion());
	if (!m_sourcePath.isEmpty()) {
		title += " — " + QFileInfo(m_sourcePath).fileName() + (m_dirty ? " *" : "");
	} else if (!m_ttpttPath.isEmpty()) {
		title += " — " + QFileInfo(m_ttpttPath).fileName() + (m_dirty ? " *" : "");
	}
	setWindowTitle(title);
}

void MainWindow::setThemeLight() { applyTheme(false); }
void MainWindow::setThemeDark() { applyTheme(true); }

void MainWindow::applyTheme(bool dark) {
	m_darkTheme = dark;
	applyAppTheme(dark);

	if (m_themeLightAction) m_themeLightAction->setChecked(!dark);
	if (m_themeDarkAction)  m_themeDarkAction->setChecked(dark);
}

void MainWindow::closeEvent(QCloseEvent *e) {
	if (!confirmDiscard()) { e->ignore(); return; }
	qInfo() << "Выход из приложения";
	m_shuttingDown = true;
	saveConfig();
	if (m_logWindow) m_logWindow->close();
	e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) { 
	if (e->mimeData()->hasUrls()) e->acceptProposedAction(); 
}
void MainWindow::dropEvent(QDropEvent *e) { 
	const auto urls = e->mimeData()->urls(); 
	if (urls.isEmpty()) return; 
	dispatchDroppedFile(urls.first().toLocalFile()); 
	e->acceptProposedAction(); 
}

void MainWindow::dispatchDroppedFile(const QString &path) {
	if (!QFileInfo::exists(path)) return;
	QString ext = QFileInfo(path).suffix().toLower();
	qInfo() << "Перетащен файл:" << QFileInfo(path).fileName();

	if (ext == "png" || ext == "bmp") { replaceImageFromPath(path); return; }
	if (ext == "pal" || ext == "act" || ext == "gpl") { importPaletteFromPath(path); return; }
	if (m_batchRunning) { setStatus("Идет операция"); return; }

	if (!confirmDiscard()) {
		qInfo() << "Открытие отменено пользователем";
		return;
	}

	if (loadTimFromPath(path, true)) return;
	qInfo() << "Не удалось открыть как TIM, пробую как сырой TIM";
	loadRawTimFromPath(path, false, true);
}

void MainWindow::openPathFromCommandLine(const QString &path) {
	if (path.isEmpty()) return;
	if (!QFileInfo::exists(path)) {
		setStatus(tr("Файл не найден: %1").arg(path));
		return;
	}
	if (m_batchRunning) {
		setStatus(tr("Идёт пакетная операция — отложено"));
		return;
	}

	qInfo() << "Открытие из командной строки:" << QFileInfo(path).fileName();
	dispatchDroppedFile(path);
}

void MainWindow::setStatus(const QString &msg) { 
	if (m_statusLabel) m_statusLabel->setText(msg);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev) {
	if (obj == m_paletteLabel && ev->type() == QEvent::MouseButtonPress) {
		QMouseEvent *me = static_cast<QMouseEvent*>(ev);
		onPaletteClick(me->pos().x());
		return true;
	}
	if (obj == m_paletteLabel && ev->type() == QEvent::MouseButtonDblClick) {
		QMouseEvent *me = static_cast<QMouseEvent*>(ev);
		const int x = me->pos().x();
		for (const auto &c : m_palCells) {
			if (x >= c.x0 && x < c.x1) { editPaletteColor(c.idx); break; }
		}
		return true;
	}

	if (obj == m_previewLabel && ev->type() == QEvent::MouseButtonPress) {
		QMouseEvent *me = static_cast<QMouseEvent*>(ev);
		if (me->button() == Qt::LeftButton) onPreviewClick(me->pos());
		return false;
	}

	if (obj == m_previewLabel && ev->type() == QEvent::Resize) {
		if (!m_previewRefreshPending) {
			m_previewRefreshPending = true;
			QTimer::singleShot(0, this, [this]() {
				m_previewRefreshPending = false;
				refreshPreview();
			});
		}
		return false;
	}
	if (obj == m_paletteLabel && ev->type() == QEvent::Resize) {
		refreshPalette();
		return false;
	}
	return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::registerFileAssociation(const QString &extIn) {
#ifdef Q_OS_WIN
	const QString ext = extIn.startsWith('.') ? extIn : ('.' + extIn);

	const FileAssocInfo *a = nullptr;
	for (const auto &entry : kFileAssocs) {
		if (QString::fromLatin1(entry.ext).compare(ext, Qt::CaseInsensitive) == 0) {
			a = &entry;
			break;
		}
	}
	if (!a) {
		showMsgBox(this, QMessageBox::Warning, tr("Ассоциация"),
				   tr("Неподдерживаемое расширение: %1").arg(ext));
		return;
	}

	const QString exePath = QDir::toNativeSeparators(
		QCoreApplication::applicationFilePath());
	const QString prog = QString::fromLatin1(a->progId);

	QSettings reg("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
	reg.setValue(ext + "/Default",		   prog);
	reg.setValue(ext + "/Content Type",	  QString::fromLatin1(a->contentType));
	if (a->isImage)
		reg.setValue(ext + "/PerceivedType", "image");

	reg.setValue(prog + "/Default",						 QString::fromLatin1(a->friendly));
	reg.setValue(prog + "/DefaultIcon/Default",			 "\"" + exePath + "\",0");
	reg.setValue(prog + "/shell/open/Default",			  "Открыть в ttptt");
	reg.setValue(prog + "/shell/open/command/Default",
				 "\"" + exePath + "\" \"%1\"");
	reg.sync();
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

	showMsgBox(this, QMessageBox::Information, tr("Ассоциация"),
			   tr("Файлы %1 связаны с ttptt.\n\n"
				  "Двойной клик по %1 в Проводнике открывает ttptt.\n"
				  "Чтобы назначить ttptt программой по умолчанию — "
				  "ПКМ по файлу → «Открыть с помощью» → "
				  "«Выбрать другое приложение».").arg(ext));
	qInfo() << "Зарегистрирована ассоциация" << ext << "→" << exePath;
#else
	Q_UNUSED(extIn);
	showMsgBox(this, QMessageBox::Information, tr("Ассоциация"),
			   tr("Ассоциация файлов реализована только для Windows."));
#endif
}

void MainWindow::unregisterFileAssociation(const QString &extIn) {
#ifdef Q_OS_WIN
	const QString ext = extIn.startsWith('.') ? extIn : ('.' + extIn);

	const FileAssocInfo *a = nullptr;
	for (const auto &entry : kFileAssocs) {
		if (QString::fromLatin1(entry.ext).compare(ext, Qt::CaseInsensitive) == 0) {
			a = &entry;
			break;
		}
	}
	if (!a) return;

	const QString prog = QString::fromLatin1(a->progId);

	{
		QSettings reg("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
		reg.remove(ext + "/OpenWithProgids/" + prog);
		reg.remove(ext + "/Default");
		reg.remove(ext + "/Content Type");
		if (a->isImage)
			reg.remove(ext + "/PerceivedType");
		reg.remove(prog);

		reg.beginGroup(ext + "/OpenWithProgids");
		const QStringList leftovers = reg.childKeys();
		reg.endGroup();
		if (leftovers.isEmpty())
			reg.remove(ext + "/OpenWithProgids");
		reg.sync();
	}
	{
		QSettings fx("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows"
					 "\\CurrentVersion\\Explorer\\FileExts\\" + ext,
					 QSettings::NativeFormat);
		fx.remove("UserChoice");
		fx.remove("OpenWithList");
		fx.remove("OpenWithProgids");
		fx.sync();
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

	showMsgBox(this, QMessageBox::Information, tr("Ассоциация"),
			   tr("Ассоциация %1 с ttptt снята (для текущего пользователя).\n\n"
				  "Если Проводник ещё показывает старую иконку или продолжает "
				  "открывать файлы в ttptt — нажмите F5 в окне Проводника "
				  "или перезапустите его.").arg(ext));
	qInfo() << "Снята ассоциация" << ext;
#else
	Q_UNUSED(extIn);
	showMsgBox(this, QMessageBox::Information, tr("Ассоциация"),
			   tr("Ассоциация файлов реализована только для Windows."));
#endif
}