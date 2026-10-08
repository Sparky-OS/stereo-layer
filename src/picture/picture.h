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
    enum class Layout { None, SideBySide, TopAndBottom, MultiView, Rows, Columns, Checkerboard };
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
    static Picture fromImages(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
        const Options &options, OutputMode output);
    static Picture fromPackedImage(const QImage &image, Layout layout, EyeOrder order);
    static Picture fromAnaglyph(const QImage &image, AnaglyphColorPair pair);
    static AnaglyphColorPair detectAnaglyph(const QImage &image);
    static QString colorDisclaimer();

    bool isStereo() const;
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
