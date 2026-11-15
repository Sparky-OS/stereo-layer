// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "stereo_layer_export.h"

namespace Stereo
{
// A film that has a picture and no audio track says so (ORDER 579). The layer holds the one sentence, in the ten languages
// of the edition, and the one rule for when it is said; each player keeps knowing its file's tracks the way its own engine
// does (Qt Multimedia's hasAudio, mpv's track-list, FFmpeg's streams) and shows the sentence in its own way of telling
// things, beside its volume control and once when the film opens. The volume control itself is never disabled or hidden.
class STEREO_LAYER_EXPORT NoSound : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString text READ text CONSTANT)
public:
    explicit NoSound(QObject *parent = nullptr);
    ~NoSound() override;
    // "This film has no sound.", in the language of the program
    static QString sentence();
    // The rule: the film is loaded (its tracks are known), it has a picture, and it has no audio track. An audio file is
    // never said to have no sound, a file whose tracks are not read yet is not said anything, and a file with an audio
    // track that is unselected or cannot be decoded is another case, told by the player with its reason.
    static bool applies(bool loaded, bool hasVideo, int audioTracks);
    QString text() const;
    Q_INVOKABLE bool saysFor(bool loaded, bool hasVideo, int audioTracks) const;
};
}
