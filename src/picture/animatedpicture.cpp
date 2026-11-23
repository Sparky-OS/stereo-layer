// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "animatedpicture.h"

namespace Stereo
{
class AnimatedPicture::Private
{
};

AnimatedPicture::AnimatedPicture(const QString &path, const Picture::Options &options)
    : d(std::make_unique<Private>())
{
    Q_UNUSED(path)
    Q_UNUSED(options)
}

AnimatedPicture::~AnimatedPicture() = default;

bool AnimatedPicture::isNull() const { return true; }
bool AnimatedPicture::isStereo() const { return false; }
QString AnimatedPicture::reason() const { return {}; }
int AnimatedPicture::frameCount() const { return 0; }
QList<int> AnimatedPicture::delays() const { return {}; }
int AnimatedPicture::loopCount() const { return 0; }
qint64 AnimatedPicture::duration() const { return 0; }
int AnimatedPicture::frameAt(qint64) const { return 0; }
Picture AnimatedPicture::pairAt(qint64, const QSize &) { return {}; }
Picture AnimatedPicture::frame(int) { return {}; }
}
