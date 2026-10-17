// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include "picture.h"
#include <QSize>
#include <QString>
#include <stereo_layer_export.h>

namespace Stereo
{
/*
 * The policy for video, shared by the players: the user's explicit choice first (a packing, or not 3D), then the stream's
 * own mark (Matroska's StereoMode, RFC 9559 section 5.1.4.1.28.3; the frame packing SEI of H.264 and H.265, Table D-8, which H.264 V15 (08/2024)
 * numbers D-8 and V16 (06/2026) D-9; the st3d box), then the frame's shape. A frame without a mark is side by side by its shape alone only when it is
 * at least three times as wide as high: two pictures next to each other (full side by side 16:9 is 3.56:1), never a single one (a scope film
 * is 2.39:1, the widest film formats stay under 3:1). Below that, and for top and bottom, the shape decides nothing and the user's choice covers the
 * rest. A still picture follows Picture's own rule instead.
 */
class STEREO_LAYER_EXPORT Video
{
public:
    // A frame at least this many times as wide as high is taken for side by side when nothing else says.
    static constexpr int SideBySideShape = 3;

    struct Layout {
        Picture::Layout layout = Picture::Layout::None;
        Picture::EyeOrder eyeOrder = Picture::EyeOrder::LeftFirst;
        Picture::Source source = Picture::Source::None;
    };

    // displaySize is the frame as shown (the sample aspect ratio applied). mark is the stream's own, in the words of
    // the Stereo3DLayout text ("mono", "sideBySideLeftFirst", "topBottomRightFirst", ...); empty when it has none.
    // options.swapEyes, the user's correction, reverses the order of whatever packing was decided.
    static Layout layout(QSize displaySize, const QString &mark, const Picture::Options &options = {});
    // The same, when the user may also say that the video is not 3D (forceMono): that choice comes before the mark, as a
    // chosen packing does (Options::inputLayout).
    static Layout layout(QSize displaySize, const QString &mark, const Picture::Options &options, bool forceMono);
    // The shape rule alone: true for a frame at least SideBySideShape times as wide as high.
    static bool sideBySideByShape(QSize displaySize);
    // Whether each view of a frame of this packing is full, shown at its own shape, or half, stretched back to the
    // frame: side by side is full when wider than a single picture can be (12:5), top and bottom when narrower (5:4); interleaved,
    // checkerboard and quincunx views are half; multiview, frame-sequential and block-laced ones full (stereo-policy.h).
    static bool fullViews(QSize displaySize, Picture::Layout layout);

    // The 3D tag of a file name (stereo-policy.h, stereo_policy_name_tag()): whole words only, so "3DMark" says nothing.
    // views: 1 full, 0 half, -1 the name does not say (stereo_policy_video_views() then decides by size).
    struct NameTag {
        Picture::Layout layout = Picture::Layout::None;
        Picture::EyeOrder eyeOrder = Picture::EyeOrder::LeftFirst;
        int views = -1;
        bool threeD = false;
    };
    static NameTag nameTag(const QString &fileName);

    // What decided whether the views are full or half
    enum class ViewsSource { Layout, Mark, Shape };
    struct Views {
        bool full = false;
        ViewsSource source = ViewsSource::Layout;
    };
    // Full or half views from what the stream says before the shape (stereo-policy.h, stereo_policy_video_views()).
    // codedSize is the frame as stored, after any crop; sampleAspectRatio is width over height of one sample (a null size
    // for 1:1); planeSize is Matroska's DisplayWidth and DisplayHeight when the track has them (RFC 9559 section 18.10),
    // else a null size; upsampledAspectRatio is the H.265 frame packing SEI's upsampled_aspect_ratio_flag (D.3.16), or -1.
    static Views views(QSize codedSize, QSize sampleAspectRatio, QSize planeSize, int upsampledAspectRatio, Picture::Layout layout);
    // The plane size (Matroska's DisplayWidth and DisplayHeight) of a side by side or top and bottom track, from the frame and the
    // sample aspect ratio FFmpeg gives it: FFmpeg's Matroska demuxer folds the plane size into the sample aspect ratio, doubled along the
    // stacked axis (stereo_policy_matroska_plane()). Pass the result as planeSize to views() for a stream that comes from Matroska through
    // FFmpeg. A null size when there is none (a bad argument, or a layout other than side by side and top and bottom).
    static QSize matroskaPlane(QSize codedSize, QSize sampleAspectRatio, Picture::Layout layout);
};
}

// The same policy for programs without Qt, or that load the library at run time: layout, order and source receive the
// integer values of Picture::Layout, Picture::EyeOrder and Picture::Source. Returns 0, or -1 for a bad argument.
extern "C" STEREO_LAYER_EXPORT int stereo_layer_video_layout(int width, int height, const char *mark, int *layout, int *order, int *source);
// Video::fullViews() in the same way: 1 for full views, 0 for half, -1 for a bad argument or no packing. Programs without
// Qt find the whole policy, the user's choice and correction included, in libstereo-policy (stereo-policy.h).
extern "C" STEREO_LAYER_EXPORT int stereo_layer_video_full_views(int width, int height, int layout);
