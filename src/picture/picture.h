// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include <QImage>
#include <QString>
#include <stereo_layer_export.h>

namespace Stereo
{
enum class OutputMode { Unknown, Mono, Stereo, Anaglyph };

class STEREO_LAYER_EXPORT Picture
{
public:
    // Every packing of RFC 9559 StereoMode and H.264/H.265 Table D-8. SideBySideQuincunx: side by side, each view
    // quincunx sampled. FrameSequential and BlockLaced alternate views in time or in a stream's blocks: a still never
    // has them, a video frame holds one view of them.
    enum class Layout { None, SideBySide, TopAndBottom, MultiView, Rows, Columns, Checkerboard, SideBySideQuincunx, FrameSequential, BlockLaced };
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
    static Picture fromAnaglyph(const QImage &image, AnaglyphColorPair pair);
    static AnaglyphColorPair detectAnaglyph(const QImage &image);
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
