// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "video.h"
#include "stereo-policy.h"

namespace Stereo
{
bool Video::sideBySideByShape(QSize displaySize) { return stereo_policy_video_side_by_side_by_shape(displaySize.width(), displaySize.height()) == 1; }

Video::Layout Video::layout(QSize displaySize, const QString &mark, const Picture::Options &options)
{
    Layout result;
    int layout = 0;
    int order = 0;
    int source = 0;
    const QByteArray value = mark.toUtf8();
    // a frame without a size has no shape to go by, but its mark and the user's choice still count
    if (stereo_policy_video_layout(qMax(1, displaySize.width()), qMax(1, displaySize.height()), value.constData(), int(options.inputLayout),
            int(options.inputOrder), options.swapEyes, &layout, &order, &source)
        == 0) {
        result.layout = Picture::Layout(layout);
        result.eyeOrder = Picture::EyeOrder(order);
        result.source = Picture::Source(source);
    }
    return result;
}

bool Video::fullViews(QSize displaySize, Picture::Layout layout)
{
    return stereo_policy_video_full_views(displaySize.width(), displaySize.height(), int(layout)) == 1;
}
}

int stereo_layer_video_layout(int width, int height, const char *mark, int *layout, int *order, int *source)
{
    return stereo_policy_video_layout(width, height, mark, 0, 0, 0, layout, order, source);
}

int stereo_layer_video_full_views(int width, int height, int layout) { return stereo_policy_video_full_views(width, height, layout); }
