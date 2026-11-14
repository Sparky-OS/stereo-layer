// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoicemoves.h"
#include "picture.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QTimer>
#include <QUrl>

using namespace Stereo;

namespace
{
const QString notifyInterface = QStringLiteral("org.kde.KDirNotify");
}

FileChoiceMoves::FileChoiceMoves(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_listening = bus.isConnected() && bus.connect(QString(), QString(), notifyInterface, QStringLiteral("FileMoved"), this, SLOT(fileMoved(QString, QString)));
    if (!m_listening) {
        const QString reason = bus.lastError().message();
        // said after the constructor returns, when the program can have connected failed()
        QTimer::singleShot(0, this, [this, reason] {
            Q_EMIT failed(tr("The 3D choice of a file cannot follow the file when it is renamed or moved: the session bus cannot be reached (%1).").arg(reason));
        });
    }
}

bool FileChoiceMoves::listening() const { return m_listening; }

void FileChoiceMoves::fileMoved(const QString &from, const QString &to)
{
    const QUrl source(from);
    const QUrl target(to);
    if (!source.isLocalFile() || !target.isLocalFile()) {
        return;
    }
    QString error;
    bool wrong = false;
    const QStringList paths = Picture::moveFileChoice(source.toLocalFile(), target.toLocalFile(), &error, &wrong);
    if (wrong) {
        Q_EMIT failed(error);
    }
    if (paths.isEmpty()) {
        return;
    }
    // every view that shows these files asks for them again, this program's and the others' (the signal FilesChanged of
    // org.kde.KDirNotify, as KIO sends it)
    QStringList urls;
    for (const QString &path : paths) {
        urls.append(QUrl::fromLocalFile(path).toString());
    }
    QDBusMessage changed = QDBusMessage::createSignal(QStringLiteral("/"), notifyInterface, QStringLiteral("FilesChanged"));
    changed << urls;
    QDBusConnection::sessionBus().send(changed);
    Q_EMIT carried(paths);
}
