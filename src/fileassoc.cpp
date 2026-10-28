// ttptt — TIM <-> Picture converter for PlayStation 1
// File: fileassoc.cpp
// Copyright (C) 2026 pav13
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fileassoc.h"
#include "timhelpers.h"

#include <QObject>
#include <QSettings>
#include <QDir>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#  include <shlobj.h>
#endif

namespace {

struct FileAssocInfo {
    const char *ext;
    const char *progId;
    const char *friendly;
    const char *contentType;
    int         resourceId;
    bool        isImage;
};

const FileAssocInfo kFileAssocs[] = {
    { ".tim",   "ttptt.tim",
      QT_TRANSLATE_NOOP("FileAssoc", "TIM (PlayStation Texture Image Map)"),
      "application/x-tim-image",     -101, true  },
    { ".ttptt", "ttptt.project",
      QT_TRANSLATE_NOOP("FileAssoc", "ttptt Project"),
      "application/x-ttptt-project", -102, false },
};

const FileAssocInfo *findAssoc(const QString &ext) {
    for (const auto &entry : kFileAssocs) {
        if (QString::fromLatin1(entry.ext).compare(ext, Qt::CaseInsensitive) == 0)
            return &entry;
    }
    return nullptr;
}

} // namespace

namespace FileAssoc {

void registerFileAssociation(const QString &extIn, QWidget *parent) {
#ifdef Q_OS_WIN
    const QString ext = extIn.startsWith('.') ? extIn : ('.' + extIn);
    const FileAssocInfo *a = findAssoc(ext);
    if (!a) {
        showMsgBox(parent, QMessageBox::Warning, QCoreApplication::translate("FileAssoc", "File Association"),
                   QCoreApplication::translate("FileAssoc", "Unsupported extension: %1").arg(ext));
        return;
    }

    const QString exePath = QDir::toNativeSeparators(
        QCoreApplication::applicationFilePath());
    const QString prog = QString::fromLatin1(a->progId);

    QSettings reg("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    reg.setValue(ext + "/Default", prog);
    reg.setValue(ext + "/Content Type", QString::fromLatin1(a->contentType));
    if (a->isImage)
        reg.setValue(ext + "/PerceivedType", "image");

    reg.setValue(prog + "/Default",
                 QCoreApplication::translate("FileAssoc", a->friendly));
    reg.setValue(prog + "/DefaultIcon/Default",
                 "\"" + exePath + "\"," + QString::number(a->resourceId));
    reg.setValue(prog + "/shell/open/Default", QCoreApplication::translate("FileAssoc", "Open in ttptt"));
    reg.setValue(prog + "/shell/open/command/Default",
                 "\"" + exePath + "\" \"%1\"");
    reg.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    showMsgBox(parent, QMessageBox::Information, QCoreApplication::translate("FileAssoc", "File Association"),
               QCoreApplication::translate("FileAssoc", "Files of type %1 are now associated with ttptt.\n\n"
                   "Double-clicking a %1 file in Explorer will open ttptt.\n"
                   "To set ttptt as the default handler — right-click the file "
                   "-> \"Open with\" -> \"Choose another app\".").arg(ext));
#else
    Q_UNUSED(extIn);
    Q_UNUSED(parent);
    showMsgBox(parent, QMessageBox::Information, QCoreApplication::translate("FileAssoc", "File Association"),
               QCoreApplication::translate("FileAssoc", "File association is implemented for Windows only."));
#endif
}

void unregisterFileAssociation(const QString &extIn, QWidget *parent) {
#ifdef Q_OS_WIN
    const QString ext = extIn.startsWith('.') ? extIn : ('.' + extIn);
    const FileAssocInfo *a = findAssoc(ext);
    if (!a) return;
    const QString prog = QString::fromLatin1(a->progId);

    {
        QSettings reg("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
        reg.remove(ext + "/OpenWithProgids/" + prog);
        reg.remove(ext + "/Default");
        reg.remove(ext + "/Content Type");
        if (a->isImage) reg.remove(ext + "/PerceivedType");
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

    showMsgBox(parent, QMessageBox::Information, QCoreApplication::translate("FileAssoc", "File Association"),
               QCoreApplication::translate("FileAssoc", "The association of %1 with ttptt has been removed "
                   "(for the current user).\n\n"
                   "If Explorer still shows the old icon or keeps opening "
                   "files in ttptt — press F5 in the Explorer window "
                   "or restart it.").arg(ext));
#else
    Q_UNUSED(extIn);
    Q_UNUSED(parent);
    showMsgBox(parent, QMessageBox::Information, QCoreApplication::translate("FileAssoc", "File Association"),
               QCoreApplication::translate("FileAssoc", "File association is implemented for Windows only."));
#endif
}

} // namespace FileAssoc