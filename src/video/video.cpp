// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "video.h"
#include "mark_p.h"

namespace Stereo
{
bool Video::sideBySideByShape(QSize displaySize)
{
    return displaySize.width() > 0 && displaySize.height() > 0 && qint64(displaySize.width()) >= qint64(displaySize.height()) * SideBySideShape;
}

Video::Layout Video::layout(QSize displaySize, const QString &mark, const Picture::Options &options)
{
    Layout result;
    if (!mark.isEmpty()) {
        result.source = Picture::Source::FileMark;
        // "mono", an anaglyph (one picture holding both views in colours), or a mark outside the set: no packing
        const Mark read = readMark(mark);
        if (read.anaglyph == Picture::AnaglyphColorPair::None) {
            result.layout = read.layout;
            result.eyeOrder = read.order;
        }
        return result;
    }
    if (options.inputLayout != Picture::Layout::None) {
        result.layout = options.inputLayout;
        result.eyeOrder = options.inputOrder;
        result.source = Picture::Source::User;
        return result;
    }
    if (sideBySideByShape(displaySize)) {
        result.layout = Picture::Layout::SideBySide;
        result.source = Picture::Source::Shape;
    }
    return result;
}
}

int stereo_layer_video_layout(int width, int height, const char *mark, int *layout, int *order, int *source)
{
    if (width <= 0 || height <= 0 || !layout || !order || !source)
        return -1;
    const auto result = Stereo::Video::layout(QSize(width, height), QString::fromUtf8(mark ? mark : ""));
    *layout = static_cast<int>(result.layout);
    *order = static_cast<int>(result.eyeOrder);
    *source = static_cast<int>(result.source);
    return 0;
}
