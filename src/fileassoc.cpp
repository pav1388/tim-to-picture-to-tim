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
    { ".tim",   "ttptt.tim",     "TIM (PlayStation Texture Image Map)",
      "application/x-tim-image",     -101, true  },
    { ".ttptt", "ttptt.project", "ttptt Project",
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
        showMsgBox(parent, QMessageBox::Warning, QObject::tr("Ассоциация"),
                   QObject::tr("Неподдерживаемое расширение: %1").arg(ext));
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

    reg.setValue(prog + "/Default", QString::fromLatin1(a->friendly));
    reg.setValue(prog + "/DefaultIcon/Default",
                 "\"" + exePath + "\"," + QString::number(a->resourceId));
    reg.setValue(prog + "/shell/open/Default", "Открыть в ttptt");
    reg.setValue(prog + "/shell/open/command/Default",
                 "\"" + exePath + "\" \"%1\"");
    reg.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    showMsgBox(parent, QMessageBox::Information, QObject::tr("Ассоциация"),
               QObject::tr("Файлы %1 связаны с ttptt.\n\n"
                          "Двойной клик по %1 в Проводнике открывает ttptt.\n"
                          "Чтобы назначить ttptt программой по умолчанию — "
                          "ПКМ по файлу -> «Открыть с помощью» -> "
                          "«Выбрать другое приложение».").arg(ext));
#else
    Q_UNUSED(extIn);
    Q_UNUSED(parent);
    showMsgBox(parent, QMessageBox::Information, QObject::tr("Ассоциация"),
               QObject::tr("Ассоциация файлов реализована только для Windows."));
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

    showMsgBox(parent, QMessageBox::Information, QObject::tr("Ассоциация"),
               QObject::tr("Ассоциация %1 с ttptt снята (для текущего пользователя).\n\n"
                          "Если Проводник ещё показывает старую иконку или продолжает "
                          "открывать файлы в ttptt — нажмите F5 в окне Проводника "
                          "или перезапустите его.").arg(ext));
#else
    Q_UNUSED(extIn);
    Q_UNUSED(parent);
    showMsgBox(parent, QMessageBox::Information, QObject::tr("Ассоциация"),
               QObject::tr("Ассоциация файлов реализована только для Windows."));
#endif
}

} // namespace FileAssoc