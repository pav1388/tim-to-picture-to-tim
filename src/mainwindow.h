// ttptt — TIM <-> Picture converter for PlayStation 1
// File: mainwindow.h
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QMainWindow>
#include <QStringList>
#include <QThread>
#include <QVector>
#include "timformat.h"

class QLabel;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QSettings;
class QAction;
class QMenu;
class QEvent;
class LogWindow;

class BatchWorker : public QObject {
    Q_OBJECT
public:
    explicit BatchWorker(QObject *parent = nullptr) : QObject(parent) {}
public slots:
    void runExtract(const QString &folder, bool anyPal, bool autoAct, bool autoPal,
                    bool autoGpl, bool saveAllPalettes, bool saveMeta, const QString &imageExt);
    void runReplace(const QString &folder, const QString &currentSourcePath);
signals:
    void progress(int current, int total, const QString &fileName);
    void logMsg(const QString &msg);
    void finished(const QString &summary, const QStringList &errors,
                  const QStringList &warnings, bool openedFileRewritten);
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void openTim();
    void openRawTim();
    void openTtptt();
    void saveTim();
    void saveTimAs();
    void saveTtptt();
    void saveTtpttAs();
    void saveImage();
    void saveAllImages();
    void replaceImage();
    void closeFile();
    void clearRecent();
    void prevImage();
    void nextImage();
    void prevPaletteRow();
    void nextPaletteRow();
    void previewScale1to1();
    void previewScaleFit();
    void toggleStp();
    void toggleLogWindow();
    void onBppChanged(int index);
	void applyMetadata();
    void resetMetadata();
    void convertTo16bpp();
    void convertTo24bpp();
    void splitImage();
    void mergeWithNext();
    void mergeWithPrev();
    void exportPalette();
    void importPalette();
    void batchExtract();
    void batchReplace();
    void showAbout();
    void updateTitle();
    void setThemeLight();
    void setThemeDark();
    void onClutDimsChanged();
    void onImageDimsChanged();
    void updateFlagsDisplay();

    void openPathFromCommandLine(const QString &path);
    void registerFileAssociation(const QString &ext);
    void unregisterFileAssociation(const QString &ext);

    void onBatchProgress(int current, int total, const QString &fileName);
    void onBatchLog(const QString &msg);
    void onBatchFinished(const QString &summary, const QStringList &errors,
                         const QStringList &warnings, bool openedFileRewritten);

private:
    void buildMenus();
    void buildUi();
    void buildParamsPanel(QWidget *parent);
    void loadConfig();
    void saveConfig();
    void addRecentFile(const QString &path);
    bool writeTimToPath(const QString &path);
    bool writeTtpttToPath(const QString &path);
    void recomputeSizeFromDims(QLineEdit *wEdit, QLineEdit *hEdit,
                                QLineEdit *sizeEdit, bool updatePixels);
    int saveAllPalettesForImage(const Tim::Group &g, int imgIdx,
                                const QString &dirPath,
                                const QString &baseStem,
                                const QString &ext);
    void updateRecentMenu();
    void applyTheme(bool dark);
    void rememberDir(const QString &path);
    QString initialDir() const;
    bool loadTimFromPath(const QString &path, bool silent = false);
    bool loadRawTimFromPath(const QString &path, bool silent = false, bool skipConfirm = false);
    bool loadTtpttFromPath(const QString &path, bool silent = false);
    QStringList importImagesForTtptt(const QString &dirPath, const QString &stem);
    bool confirmDiscard();
    bool replaceImageFromPath(const QString &path);
    bool importPaletteFromPath(const QString &path);
    void dispatchDroppedFile(const QString &path);
    void convertBpp(int targetBpp);
    void mergeImages(bool withNext);
    void rebuildAndReparse(bool preserveEntry = true);
    void refreshAll();
    void refreshPreview();
    void refreshPalette();
    void refreshParams();
    void setStatus(const QString &msg);
    void updateNavButtons();
    void updateImagePixelsLabel();
    void onPaletteClick(int x);
    void onPreviewClick(const QPoint &labelPos);
    void editPaletteColor(int idx);
    Tim::Group *currentGroup(int *imgIdx = nullptr);
    const Tim::Group *currentGroup(int *imgIdx = nullptr) const;
    QVector<Tim::Group> m_groups;
    QVector<QPair<int,int>> m_entries;
    int m_currentEntry = -1;
    int m_paletteRow = 0;
    bool m_zoomFit = true;
    bool m_showStp = false;
    bool m_dirty = false;
    bool m_batchRunning = false;
    bool m_suppressSizeTrace = false;
    bool m_darkTheme = false;
    bool m_previewRefreshPending = false;
    bool m_autoSaveAct = true;
    bool m_autoSavePal = false;
    bool m_autoSaveGpl = false;
    bool m_autoSaveTtptt = true;
    bool m_saveAllPalettesAuto = false;
    bool m_batchSaveAct = false;
    bool m_batchSavePal = false;
    bool m_batchSaveGpl = false;
    bool m_batchSaveTtptt = false;
    bool m_saveAllPalettesBatch = false;
    bool m_showLog = false;
    bool m_shuttingDown = false;

    QString m_batchImageFormat = "png";
    QSize m_previewSrcSize;
    QSize m_previewDisplaySize;
    QString m_sourcePath;
    QString m_ttpttPath;
    QString m_sourceStem;
    QStringList m_recentFiles;
    QString m_lastDir;
    QString defaultSavePath(const QString &filename) const;

    QAction *m_saveTtpttAction = nullptr;
    QAction *m_saveTtpttAsAction = nullptr;
    QAction *m_autoSaveTtpttAction = nullptr;
    QAction *m_manualAllPalettesAction = nullptr;

    QLabel *m_previewLabel = nullptr;
    QLabel *m_paletteLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_palInfoLabel = nullptr;
    QLabel *m_palRowLabel = nullptr;
    QLabel *m_navLabel = nullptr;
    QLabel *m_flagsDisplay = nullptr;
    QLabel *m_imagePixelsLabel = nullptr;

    QComboBox *m_bppCombo = nullptr;
    QCheckBox *m_hasClutCheck = nullptr;
    QCheckBox *m_hasImageCheck = nullptr;
    QLineEdit *m_clutSizeEdit = nullptr;
    QLineEdit *m_clutXEdit = nullptr;
    QLineEdit *m_clutYEdit = nullptr;
    QLineEdit *m_clutWidthEdit = nullptr;
    QLineEdit *m_clutHeightEdit = nullptr;
    QLineEdit *m_imgSizeEdit = nullptr;
    QLineEdit *m_imgXEdit = nullptr;
    QLineEdit *m_imgYEdit = nullptr;
    QLineEdit *m_imgWidthEdit = nullptr;
    QLineEdit *m_imgHeightEdit = nullptr;

    QPushButton *m_palPrevBtn = nullptr;
    QPushButton *m_palNextBtn = nullptr;
    QPushButton *m_imgPrevBtn = nullptr;
    QPushButton *m_imgNextBtn = nullptr;
    QPushButton *m_stpBtn = nullptr;
    
	QMenu *m_recentSubmenu = nullptr;
    QMenu *m_fileMenu = nullptr;
	
    QAction *m_stpAction = nullptr;
    QAction *m_logAction = nullptr;
    QAction *m_autoActAction = nullptr;
    QAction *m_autoPalAction = nullptr;
    QAction *m_autoGplAction = nullptr;
    QAction *m_themeLightAction = nullptr;
    QAction *m_themeDarkAction = nullptr;

    LogWindow *m_logWindow = nullptr;
    QSettings *m_settings = nullptr;
    QThread *m_batchThread = nullptr;

    struct PalCell { int x0, x1; int r, g, b; int idx; quint16 raw; };
    QVector<PalCell> m_palCells;
};