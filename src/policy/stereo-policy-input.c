/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stereo-policy.h"

/* One input list for the C, GTK, Qt Widgets and QML consumers.
 * StereoMode: RFC 9559 5.1.4.1.28.3; frame packing: H.264/H.265 Table D-8.
 * Native multiview and separate depth require another decoded image and are
 * discovered, not selectable interpretations of one packed picture. */
static const struct stereo_policy_input_format formats[] = {
    {"automatic", "Automatic", 0, 0, 0, 0, 0, 0},
    {"mono", "Not 3D", 0, 0, 0, 0, 3, 0},
    {"sideBySideLeftFirst", "Side by side, left eye first", 1, 1, 0, 0, 0, 0},
    {"sideBySideRightFirst", "Side by side, right eye first", 1, 1, 1, 0, 0, 0},
    {"topBottomLeftFirst", "Top and bottom, left eye first", 1, 2, 0, 0, 0, 0},
    {"topBottomRightFirst", "Top and bottom, right eye first", 1, 2, 1, 0, 0, 0},
    {"rowInterleavedLeftFirst", "Rows, left eye first", 2, 4, 0, 0, 0, 0},
    {"rowInterleavedRightFirst", "Rows, right eye first", 2, 4, 1, 0, 0, 0},
    {"columnInterleavedLeftFirst", "Columns, left eye first", 2, 5, 0, 0, 0, 0},
    {"columnInterleavedRightFirst", "Columns, right eye first", 2, 5, 1, 0, 0, 0},
    {"checkerboardLeftFirst", "Checkerboard, left eye first", 2, 6, 0, 0, 0, 0},
    {"checkerboardRightFirst", "Checkerboard, right eye first", 2, 6, 1, 0, 0, 0},
    {"sideBySideQuincunxLeftFirst", "Side by side quincunx, left eye first", 2, 7, 0, 0, 0, 0},
    {"sideBySideQuincunxRightFirst", "Side by side quincunx, right eye first", 2, 7, 1, 0, 0, 0},
    {"frameSequentialLeftFirst", "Frames in turn, left eye first", 3, 8, 0, 0, 0, 1},
    {"frameSequentialRightFirst", "Frames in turn, right eye first", 3, 8, 1, 0, 0, 1},
    {"blockLacedLeftFirst", "Both views in one block, left eye first", 3, 9, 0, 0, 0, 1},
    {"blockLacedRightFirst", "Both views in one block, right eye first", 3, 9, 1, 0, 0, 1},
    {"anaglyphCyanRed", "Red/cyan", 4, 0, 0, 1, 0, 0},
    {"anaglyphGreenMagenta", "Green/magenta", 4, 0, 0, 2, 0, 0},
    {"anaglyphAmberBlue", "Amber/blue", 4, 0, 0, 3, 0, 0},
    {"depthSideBySidePictureFirst", "Side by side, picture first", 5, 10, 0, 0, 0, 0},
    {"depthSideBySideDepthFirst", "Side by side, depth first", 5, 10, 1, 0, 0, 0},
    {"depthTopBottomPictureFirst", "Top and bottom, picture first", 5, 11, 0, 0, 0, 0},
    {"depthTopBottomDepthFirst", "Top and bottom, depth first", 5, 11, 1, 0, 0, 0},
    {"depthGraphicsTopBottom", "Four rows: picture, depth, graphics, graphics depth", 5, 12, 0, 0, 0, 0},
    {"depthBackgroundQuarters", "Four quarters: picture, depth, background, background depth", 5, 13, 0, 0, 0, 0},
};
size_t stereo_policy_input_format_count(void) { return sizeof(formats) / sizeof(formats[0]); }
const struct stereo_policy_input_format *stereo_policy_input_format_at(size_t index)
{
    return index < stereo_policy_input_format_count() ? &formats[index] : NULL;
}
