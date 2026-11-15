// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "nosound.h"

#include <QCoreApplication>

namespace Stereo
{
NoSound::NoSound(QObject *parent)
    : QObject(parent)
{
}

NoSound::~NoSound() = default;

QString NoSound::sentence() { return QCoreApplication::translate("Stereo::NoSound", "This film has no sound."); }

bool NoSound::applies(bool loaded, bool hasVideo, int audioTracks) { return loaded && hasVideo && audioTracks <= 0; }

QString NoSound::text() const { return sentence(); }

bool NoSound::saysFor(bool loaded, bool hasVideo, int audioTracks) const { return applies(loaded, hasVideo, audioTracks); }
}
