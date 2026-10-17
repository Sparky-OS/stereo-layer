// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "video.h"
#include "stereo-policy.h"

namespace Stereo
{
bool Video::sideBySideByShape(QSize displaySize) { return stereo_policy_video_side_by_side_by_shape(displaySize.width(), displaySize.height()) == 1; }

Video::Layout Video::layout(QSize displaySize, const QString &mark, const Picture::Options &options)
{
    return layout(displaySize, mark, options, false);
}

Video::Layout Video::layout(QSize displaySize, const QString &mark, const Picture::Options &options, bool forceMono)
{
    Layout result;
    int layout = 0;
    int order = 0;
    int source = 0;
    const QByteArray value = mark.toUtf8();
    // a frame without a size has no shape to go by, but its mark and the user's choice still count
    if (stereo_policy_video_layout(qMax(1, displaySize.width()), qMax(1, displaySize.height()), value.constData(),
            forceMono ? STEREO_POLICY_USER_FORCE_2D : int(options.inputLayout), int(options.inputOrder), options.swapEyes, &layout, &order, &source)
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

Video::NameTag Video::nameTag(const QString &fileName)
{
    NameTag result;
    int layout = 0, order = 0, views = -1, threeD = 0;
    const QByteArray name = fileName.toUtf8();
    stereo_policy_name_tag(name.constData(), &layout, &order, &views, &threeD);
    result.layout = Picture::Layout(layout);
    result.eyeOrder = Picture::EyeOrder(order);
    result.views = views;
    result.threeD = threeD == 1;
    return result;
}

QSize Video::matroskaPlane(QSize codedSize, QSize sampleAspectRatio, Picture::Layout layout)
{
    int width = 0;
    int height = 0;
    if (stereo_policy_matroska_plane(
            codedSize.width(), codedSize.height(), sampleAspectRatio.width(), sampleAspectRatio.height(), int(layout), &width, &height)
        != 0) {
        return {};
    }
    return { width, height };
}

Video::Views Video::views(QSize codedSize, QSize sampleAspectRatio, QSize planeSize, int upsampledAspectRatio, Picture::Layout layout)
{
    Views result;
    int decidedBy = 0;
    const int full = stereo_policy_video_views(codedSize.width(), codedSize.height(), sampleAspectRatio.width(), sampleAspectRatio.height(),
        planeSize.width(), planeSize.height(), upsampledAspectRatio, int(layout), &decidedBy);
    result.full = full == 1;
    result.source = decidedBy == STEREO_POLICY_VIEWS_BY_MARK ? ViewsSource::Mark
        : decidedBy == STEREO_POLICY_VIEWS_BY_SHAPE          ? ViewsSource::Shape
                                                             : ViewsSource::Layout;
    return result;
}
}

int stereo_layer_video_layout(int width, int height, const char *mark, int *layout, int *order, int *source)
{
    return stereo_policy_video_layout(width, height, mark, 0, 0, 0, layout, order, source);
}

int stereo_layer_video_full_views(int width, int height, int layout) { return stereo_policy_video_full_views(width, height, layout); }
