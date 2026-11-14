// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QObject>
#include <QStringList>
#include <stereo_layer_export.h>

namespace Stereo
{
// Keeps a file's per-file choice with the file when a program renames or moves it. A file manager, or any program that
// renames or moves media files, makes one of these and keeps it: it hears every rename and move the desktop's file
// jobs announce on the session bus (the signal FileMoved of the interface org.kde.KDirNotify, which KIO sends for a
// rename, for each file of a move, for a folder moved as a whole and for their undo) and carries the choice
// (Picture::moveFileChoice()). Several programs may each have one: the store changes nothing the second time. A copy
// carries nothing, and neither does a move the desktop's file jobs did not do (a command in a terminal): that file
// loses its choice, as it loses a player's resume position. Files that are not local are left alone.
class STEREO_LAYER_EXPORT FileChoiceMoves : public QObject
{
    Q_OBJECT
public:
    explicit FileChoiceMoves(QObject *parent = nullptr);

    // Whether the session bus was reached; without it no move is heard, and failed() said so once.
    bool listening() const;

Q_SIGNALS:
    // The choice of these files, by their new paths, came with them: show them again.
    void carried(const QStringList &paths);
    // A choice could not follow its file, or no move can be heard: message says which, and why.
    void failed(const QString &message);

private Q_SLOTS:
    void fileMoved(const QString &from, const QString &to);

private:
    bool m_listening = false;
};
}
