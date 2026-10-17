// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include <QImage>
#include <QString>
#include <stereo_layer_export.h>

namespace Stereo
{
enum class OutputMode { Unknown, Mono, Stereo, Anaglyph };

// How the views are drawn from a picture and its depth (libstereo-dibr, stereo-dibr.h): every path a choice. range,
// zero and shift are for depth whose mark gives none (pixels of a view 1920 wide; zero from 0, the farthest, to 1).
struct DepthOptions {
    enum class View { Left, Center } view = View::Left;
    enum class Warp { Forward, Backward, Layered } warp = Warp::Forward;
    enum class Fill { Background, DepthAided, Telea, None } fill = Fill::Background;
    bool smooth = false;
    double sigmaH = 10.0, sigmaV = 30.0;
    int radius = 5;
    double range = 58.0, zero = 0.5, shift = 0.0;
};

class STEREO_LAYER_EXPORT Picture
{
public:
    // Every packing of RFC 9559 StereoMode and H.264/H.265 Table D-8 (H.264 V15, 08/2024; D-9 in H.264 V16, 06/2026).
    // SideBySideQuincunx: side by side, each view quincunx sampled. FrameSequential and BlockLaced alternate views in time or
    // in a stream's blocks: a still never has
    // them, a video frame holds one view of them.
    // The depth layouts (docs/depth-marks.md) hold a picture and its depth map, from which the views are drawn
    // (fromDepth()): next to each other, one above the other, four rows (picture, depth, graphics, graphics depth: HDMI
    // 1.4a "L + depth + graphics + graphics-depth"), four quarters (picture, depth, background, background depth), or the
    // depth map as an image of its own. For them EyeOrder::LeftFirst is the picture first, RightFirst the depth first.
    enum class Layout {
        None,
        SideBySide,
        TopAndBottom,
        MultiView,
        Rows,
        Columns,
        Checkerboard,
        SideBySideQuincunx,
        FrameSequential,
        BlockLaced,
        DepthSideBySide,
        DepthTopAndBottom,
        DepthGraphics,
        DepthBackground,
        DepthSeparate
    };
    enum class EyeOrder { LeftFirst, RightFirst };
    enum class AnaglyphColorPair { None, RedCyan, GreenMagenta, AmberBlue };
    enum class Source { None, FileMark, User, Format, Detection, Shape };
    enum class View { Stereo, LeftOnly, RightOnly, Mono };
    struct Options {
        Layout inputLayout = Layout::None;
        EyeOrder inputOrder = EyeOrder::LeftFirst;
        AnaglyphColorPair anaglyph = AnaglyphColorPair::None;
        View view = View::Stereo;
        bool swapEyes = false;
    };

    static Picture load(const QString &path, const Options &options, OutputMode output);
    // For pictures shown small, such as thumbnails: the policy of load() on an output that is not stereo, each image
    // decoded to fit within bound where the reader can, and the views scaled so that the pair side by side fits bound.
    // A file that cannot be a pair (no mark, no stereo format, no stereo shape) is not decoded: the result is empty.
    static Picture loadPair(const QString &path, const QSize &bound, const Options &options);
    static Picture fromImages(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
        const Options &options, OutputMode output);
    static Picture fromPackedImage(const QImage &image, Layout layout, EyeOrder order);
    using DepthOptions = Stereo::DepthOptions;
    // The two views of picture drawn from depth, a gray image of any size (8 or 16 bits), read by depthMark, the
    // Stereo3DDepth mark of docs/depth-marks.md ("nearness" when empty: larger samples nearer). layer and layerDepth,
    // when given, are a second layer of the picture's size: the background it hides (layerIsBackground) or graphics over
    // it. The views have the picture's size; null when the images cannot be read or the mark is malformed.
    static Picture fromDepth(const QImage &picture, const QImage &depth, const QString &depthMark, const DepthOptions &options = {},
        const QImage &layer = {}, const QImage &layerDepth = {}, bool layerIsBackground = true);
    static Picture fromAnaglyph(const QImage &image, AnaglyphColorPair pair);
    static AnaglyphColorPair detectAnaglyph(const QImage &image);
    // Whether the two halves of an unmarked picture are views of one scene, by its pixels, on a reduced copy
    // (stereo_policy_picture_pair_by_content()). Shape only says a picture may be a pair: an unmarked picture is one
    // unless this says it is.
    static bool pairByContent(const QImage &image);
    static QString colorDisclaimer();
    // The user's correction stored with a file whose mark has its eyes the wrong way round (stereo-policy.h,
    // stereo_policy_file_swap_eyes()). load() and loadPair() apply it over the file's mark, on top of
    // Options::swapEyes. setUserSwapEyes() returns false where the file system keeps no extended attributes.
    static bool userSwapEyes(const QString &path);
    static bool setUserSwapEyes(const QString &path, bool swap);
    static bool canStoreUserSwapEyes(const QString &path);

    bool isStereo() const;
    // Both views side by side, left first, each at its own size, marked "sideBySideLeftFirst" in the image's
    // Stereo3DLayout text; a null image when this is not a pair.
    QImage sideBySide() const;
    Picture swapped() const;
    QImage original;
    QImage left;
    QImage right;
    Layout layout = Layout::None;
    EyeOrder eyeOrder = EyeOrder::LeftFirst;
    AnaglyphColorPair anaglyph = AnaglyphColorPair::None;
    Source source = Source::None;
    bool approximateColor = false;
};
}
