// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include "picture.h"

#include <QList>
#include <QSize>
#include <memory>

namespace Stereo
{
// An animated picture handed out a frame at a time, each frame as one pair: a timed raster through the edition's image
// readers (JPEG XL, ISO/IEC 18181-1:2024 D.3.4 and F.2, marked by JXLStereo; any other animated format the readers know)
// and an SVG drawing by the SVGStereo 0.4 rules ("Animated drawings": one document time for both views). The caller keeps
// one clock and asks for the pair at a time on it; both views of a pair always come from one frame, decoded or drawn once.
// The views are taken by the decision Picture::load() makes for the file (the person's choice for it, then its mark,
// then its shape), made once on the first frame and kept for every frame. A file that cannot be shown, or a frame that
// cannot be produced, gives no picture and a reason the caller can show the person.
class STEREO_LAYER_EXPORT AnimatedPicture
{
public:
    explicit AnimatedPicture(const QString &path, const Picture::Options &options = {});
    ~AnimatedPicture();
    AnimatedPicture(const AnimatedPicture &) = delete;
    AnimatedPicture &operator=(const AnimatedPicture &) = delete;

    // Nothing of the file can be shown; reason() says why.
    bool isNull() const;
    // Whether the frames are pairs.
    bool isStereo() const;
    // Why the file, or the last frame asked for, could not be produced; empty while everything could.
    QString reason() const;
    // What the file declares: its frames, each frame's time in milliseconds, and how many times the animation plays again
    // after the first time (QImageReader::loopCount(): -1 for ever). A drawing has no frames of its own: frameCount() is 0,
    // delays() is empty, and duration() is its document's.
    int frameCount() const;
    QList<int> delays() const;
    int loopCount() const;
    qint64 duration() const;
    // The frame shown msecs after the start of the caller's clock; the last frame once the last play has ended.
    int frameAt(qint64 msecs) const;
    // Clock time at which another frame can be shown, or -1 after the last frame.
    qint64 nextFrameTime(qint64 msecs) const;
    // Both views of what is shown at msecs, taken from that one frame. A raster's views have the file's own size and
    // precision (QImage::Format_RGBA64 for samples of more than 8 bits); a drawing is drawn at eyeSize a view, or at its own
    // size when eyeSize is empty. Not stereo: original holds the frame. Null, with reason() set, when it cannot be produced.
    Picture pairAt(qint64 msecs, const QSize &eyeSize = {});
    // The same by frame number, for a raster.
    Picture frame(int index);

private:
    class Private;
    std::unique_ptr<Private> d;
};
}
