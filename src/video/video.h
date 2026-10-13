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
 * The policy for video, shared by the players: the stream's own mark first (Matroska's StereoMode, RFC 9559
 * section 5.1.4.1.28.3; the frame packing SEI of H.264 and H.265, Table D-8; the st3d box), then the user's choice,
 * then the frame's shape. A frame without a mark is side by side by its shape alone only when it is at least three
 * times as wide as high: two pictures next to each other (full side by side 16:9 is 3.56:1), never a single one
 * (a scope film is 2.39:1, the widest film formats stay under 3:1). Below that, and for top and bottom, the shape
 * decides nothing and the user's choice covers the rest. A still picture follows Picture's own rule instead.
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
    static Layout layout(QSize displaySize, const QString &mark, const Picture::Options &options = {});
    // The shape rule alone: true for a frame at least SideBySideShape times as wide as high.
    static bool sideBySideByShape(QSize displaySize);
};
}

// The same policy for programs without Qt, or that load the library at run time: layout, order and source receive the
// integer values of Picture::Layout, Picture::EyeOrder and Picture::Source. Returns 0, or -1 for a bad argument.
extern "C" STEREO_LAYER_EXPORT int stereo_layer_video_layout(int width, int height, const char *mark, int *layout, int *order, int *source);
