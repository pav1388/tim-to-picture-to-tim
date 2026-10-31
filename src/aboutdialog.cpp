// ttptt — TIM <-> Picture converter for PlayStation 1
// File: aboutdialog.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "aboutdialog.h"
#include "mainwindow.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

// --- Настройки модуля AboutDialog (префикс AD_) ---

#define AD_DIALOG_WIDTH       640
#define AD_DIALOG_HEIGHT      480
#define AD_HEADER_ICON_SIZE   64

#define AD_HOMEPAGE_URL       "https://github.com/pav1388/tim-to-picture-to-tim"

static const char *kAboutTitle = QT_TRANSLATE_NOOP("aboutdialog",
	"ttptt: tim-to-picture-to-tim by pav13, 2026"
);

static const char *kAboutSubtitle = QT_TRANSLATE_NOOP("aboutdialog",
	"TIM <-> Picture converter for PlayStation 1.\n"
	"License: GPL-3.0-or-later"
);

static const char *kAboutFeatures = QT_TRANSLATE_NOOP("aboutdialog",
	"Features:\n"
	"  * Support for 4/8/16/24-bit TIM\n"
	"  * Multi-row CLUT, multiple images per group\n"
	"  * 16bpp <-> 24bpp conversion, image split/merge\n"
	"  * Palettes: JASC-PAL/ACT/GPL/PNG/BMP\n"
	"  * Palette editor: double-click a color to open the color picker\n"
	"  * Current palette index and pixel info shown in the status bar\n"
	"  * Image save formats: PNG and BMP\n"
	"    (for 16bpp, STP bits are not preserved in BMP — the program warns you)\n"
	"  * Auto-save palettes next to the image\n"
	"    (configured separately for manual saves and batch runs)\n"
	"  * With \"all palettes\" enabled and a multi-row CLUT, each\n"
	"    file gets a _palNN suffix (NN is 1-based, same as in the UI)\n"
	"  * Auto-save metadata next to the image\n"
	"    (configured separately for manual saves and batch runs)\n"
	"  * Batch operations (image extract / replace)\n"
	"  * \"Save all images from TIM\" — exports all image blocks in one go\n"
	"  * *.ttptt metadata stores everything that cannot be recovered from\n"
	"    the picture alone (flags, CLUT, image block headers, prefix before\n"
	"    the first TIM, trailing data, warnings). Pixel data is not\n"
	"    duplicated — the image file remains the source of truth\n"
	"  * Full preservation of data before the first header and after\n"
	"    each image block\n"
	"  * Opening raw TIM files without a signature\n"
	"  * Robust parser (broken headers, padding, truncated files)\n"
	"  * Dark/light UI themes\n"
	"  * Recent files list\n"
	"  * File associations (Windows, no admin rights required):\n"
	"       .tim   — PlayStation Texture Image Map\n"
	"       .ttptt — metadata project\n"
	"    You can associate one extension without touching the other\n"
	"  * Drag & Drop:\n"
	"       .PNG/.BMP -> replace image\n"
	"       .PAL/.ACT/.GPL -> import palette\n"
	"       .TTPTT -> open metadata\n"
	"       other files -> open as TIM or as raw TIM\n"
	"  * Log window (Settings menu)"
	"  * Note: part of the code was generated or refined with the help of AI (DeepSeek)"
);

static const char *kAboutHomage = QT_TRANSLATE_NOOP("aboutdialog",
	"Homage to TIMViewer 1.04b (rveach, 2004)"
);

static const char *kAboutTimStructure = QT_TRANSLATE_NOOP("aboutdialog",
	"========== Texture Image Map structure ==========\n"
	"\n"
	"[1] GLOBAL HEADER (8 bytes)\n"
	"    Bytes 0-3:  magic = 0x10 0x00 0x00 0x00\n"
	"    Bytes 4-7:  flags (32-bit little-endian integer)\n"
	"    Bits 0-2:   bpp (0=4bit, 1=8bit, 2=16bit, 3=24bit)\n"
	"    Bit 3:      has_clut (1 = palette present)\n"
	"    Bit 4:      has_image (1 = image present)\n"
	"    Bits 5-31:  reserved (usually 0)\n"
	"\n"
	"[2] CLUT BLOCK (if has_clut = 1)\n"
	"  Header (12 bytes):\n"
	"    size         4 bytes  = 12 + data_size\n"
	"    x, y         2+2 bytes = VRAM coordinates of the palette\n"
	"    width_words  2 bytes   = width in 16-bit words\n"
	"    height       2 bytes   = number of palette rows\n"
	"  Data (width_words * height * 2 bytes):\n"
	"    BGR555 format: [STP|B4 B3|G4 G3 G2|R4 R3 R2 R1 R0]\n"
	"    STP (bit 15) — semi-transparency flag (1 = semi-transparent)\n"
	"    Multi-row CLUT: each row is a separate palette\n"
	"\n"
	"[3] IMAGE BLOCKS (one or more, if has_image = 1)\n"
	"  Header (12 bytes):\n"
	"    size         4 bytes  = 12 + data_size\n"
	"    x, y         2+2 bytes = VRAM coordinates of the image\n"
	"    width_words  2 bytes   = width in 16-bit words\n"
	"    height       2 bytes   = height in pixels\n"
	"  Data (depends on bpp):\n"
	"    4-bit:  2 pixels per byte (low nibble first)\n"
	"            width in pixels = width_words * 4\n"
	"            indices into CLUT (max 16 colors)\n"
	"    8-bit:  1 pixel per byte\n"
	"            width in pixels = width_words * 2\n"
	"            indices into CLUT (max 256 colors)\n"
	"    16-bit: 2 bytes per pixel (BGR555 + STP)\n"
	"            width in pixels = width_words\n"
	"            direct color + semi-transparency\n"
	"    24-bit: 3 bytes per pixel (RGB)\n"
	"            width in pixels = (width_words * 2) / 3\n"
	"            direct color, no transparency\n"
	"\n"
	"[4] PREFIX AND TRAILING DATA\n"
	"  Before the first TIM header there may be an arbitrary prefix\n"
	"  (game container header, padding, etc.). It is stored in the\n"
	"  metadata (\"prefix\" field of the first group) and restored during\n"
	"  rebuild — so the file round-trips without loss.\n"
	"\n"
	"  After all blocks there may be:\n"
	"    - Padding to 4-byte alignment\n"
	"    - Another TIM (signature 0x10 0x00 0x00 0x00)\n"
	"    - Garbage / game data\n"
	"  These trailing bytes are stored in the \"extra\" field of the\n"
	"  corresponding group."
);

static const char *kAboutVram = QT_TRANSLATE_NOOP("aboutdialog",
	"=================== PS1 VRAM ===================\n"
	"  PS1 video memory: 1024 x 512 pixels (in 16-bit words)\n"
	"  The x, y coordinates specify where to load the data in VRAM.\n"
	"  CLUTs are usually placed in the lower part of VRAM (y >= 480)."
);

static const char *kAboutMetadata = QT_TRANSLATE_NOOP("aboutdialog",
	"=================== *.ttptt metadata ===================\n"
	"  JSON file (UTF-8), stores metadata ONLY (pixels are not duplicated):\n"
	"  {\n"
	"    \"version\":         1,\n"
	"    \"app\":             \"ttptt\",\n"
	"    \"sourcePath\":      \"<source .tim, if any>\",\n"
	"    \"paletteRow\":      <current CLUT row>,\n"
	"    \"currentEntry\":    <current image block>,\n"
	"    \"showStp\":         true|false,\n"
	"    \"groups\": [\n"
	"      {\n"
	"        \"flags\":       \"<uint32, as string>\",\n"
	"        \"bpp\":         0..3,\n"
	"        \"hasClut\":     true|false,\n"
	"        \"hasImage\":    true|false,\n"
	"        \"startOffset\": <offset in the source .tim>,\n"
	"        \"clutHeader\":  { \"size\": \"<uint32>\", \"x\", \"y\",\n"
	"                         \"widthWords\", \"height\" },\n"
	"        \"clut\":        \"<base64 CLUT data>\",\n"
	"        \"images\": [\n"
	"          { \"header\":  { \"size\": \"<uint32>\", \"x\", \"y\",\n"
	"                         \"widthWords\", \"height\" } }\n"
	"        ],\n"
	"        \"extra\":       \"<base64 trailing data>\",\n"
	"        \"prefix\":      \"<base64 prefix before the first TIM\n"
	"                         (first group only)>\",\n"
	"        \"warnings\":    [ \"…\" ]\n"
	"      }\n"
	"    ]\n"
	"  }\n"
	"\n"
	"  When a .ttptt is opened, the following are searched for in its directory:\n"
	"    For 4/8bpp with a multi-row CLUT — first the CURRENT palette's file\n"
	"    (NN is the 1-based CLUT row number, same as in the UI):\n"
	"      <name>_palNN.png|bmp\n"
	"      <name>_imgNN_palNN.png|bmp    (NN is the global image block index)\n"
	"    Then the base name (for a single palette or as a fallback):\n"
	"      <name>.png|bmp    (if the project has only one image)\n"
	"      <name>_imgNN.png|bmp\n"
	"  Found images are decoded with the current palette and re-encoded into\n"
	"  image data. If no PNG/BMP is found — a correctly sized placeholder\n"
	"  is inserted."
);

static const char *kAboutNotes = QT_TRANSLATE_NOOP("aboutdialog",
	"=================== Notes ===================\n"
	"  * size always includes the 12-byte block header\n"
	"  * width_words counts 16-bit words, NOT pixels\n"
	"  * 4-bit: 2 pixels per byte, the low nibble is the first pixel\n"
	"  * 16-bit STP=1 means semi-transparency (bg r a 128)\n"
	"  * A single file may contain several TIMs in a row\n"
	"  * A single group may contain several image blocks\n"
	"  * The prefix before the first TIM and the trailing bytes after the\n"
	"    blocks are preserved on save — the file passes through ttptt\n"
	"    without losing \"invisible\" data"
);

namespace {

QIcon effectiveWindowIcon(QWidget *parent)
{
	return parent ? parent->windowIcon() : QIcon();
}

QString buildAboutText()
{
	return QStringList{
		QCoreApplication::translate("aboutdialog", kAboutFeatures),
		QCoreApplication::translate("aboutdialog", kAboutHomage),
		QCoreApplication::translate("aboutdialog", kAboutTimStructure),
		QCoreApplication::translate("aboutdialog", kAboutVram),
		QCoreApplication::translate("aboutdialog", kAboutMetadata),
		QCoreApplication::translate("aboutdialog", kAboutNotes),
	}.join("\n\n");
}

} // namespace

QDialog *createAboutDialog(QWidget *parent)
{
	auto *dlg = new QDialog(parent);
	dlg->setWindowTitle(QCoreApplication::translate("aboutdialog", "About"));
	const QIcon appIcon = effectiveWindowIcon(parent);
	dlg->setWindowIcon(appIcon);
	dlg->setWindowFlags(dlg->windowFlags() & ~Qt::WindowContextHelpButtonHint);
	dlg->resize(AD_DIALOG_WIDTH, AD_DIALOG_HEIGHT);

	auto *layout = new QVBoxLayout(dlg);

	auto *headerRow = new QHBoxLayout();

	auto *iconLabel = new QLabel(dlg);
	if (!appIcon.isNull()) {
		iconLabel->setPixmap(
			appIcon.pixmap(AD_HEADER_ICON_SIZE, AD_HEADER_ICON_SIZE));
		iconLabel->setFixedSize(AD_HEADER_ICON_SIZE, AD_HEADER_ICON_SIZE);
		iconLabel->setAlignment(Qt::AlignCenter);
	}
	headerRow->addWidget(iconLabel, 0, Qt::AlignTop);

	auto *headerColumn = new QVBoxLayout();

	auto *titleLabel = new QLabel(dlg);
	titleLabel->setTextFormat(Qt::RichText);
	titleLabel->setWordWrap(true);
	titleLabel->setText(QStringLiteral("<b>%1</b>").arg(
		QCoreApplication::translate("aboutdialog", kAboutTitle)));
	headerColumn->addWidget(titleLabel);

	auto *subtitleLabel = new QLabel(dlg);
	subtitleLabel->setWordWrap(true);
	subtitleLabel->setText(
		QCoreApplication::translate("aboutdialog", kAboutSubtitle));
	headerColumn->addWidget(subtitleLabel);

	auto *linkLabel = new QLabel(dlg);
	linkLabel->setTextFormat(Qt::RichText);
	linkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
	linkLabel->setOpenExternalLinks(true);
	linkLabel->setText(QStringLiteral("<a href=\"%1\">%1</a>")
	                       .arg(QStringLiteral(AD_HOMEPAGE_URL)));
	headerColumn->addWidget(linkLabel);

	headerColumn->addStretch();
	headerRow->addLayout(headerColumn, 1);
	layout->addLayout(headerRow);

	auto *txt = new QPlainTextEdit(dlg);
	txt->setReadOnly(true);
	txt->setPlainText(buildAboutText());
	layout->addWidget(txt);

	auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
	if (auto *b = bb->button(QDialogButtonBox::Close))
		b->setText(QCoreApplication::translate("aboutdialog", "Close"));
	QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
	layout->addWidget(bb);

	return dlg;
}

void showAboutDialog(QWidget *parent)
{
	QDialog *dlg = createAboutDialog(parent);
	dlg->exec();
	delete dlg;
}