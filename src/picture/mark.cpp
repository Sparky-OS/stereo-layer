// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "mark_p.h"

namespace Stereo
{
Mark readMark(const QString &value)
{
    struct Packing {
        QLatin1StringView name;
        Picture::Layout layout;
    };
    static constexpr Packing packings[] = {
        { QLatin1StringView("sideBySide"), Picture::Layout::SideBySide },
        { QLatin1StringView("sideBySideQuincunx"), Picture::Layout::SideBySideQuincunx },
        { QLatin1StringView("topBottom"), Picture::Layout::TopAndBottom },
        { QLatin1StringView("checkerboard"), Picture::Layout::Checkerboard },
        { QLatin1StringView("rowInterleaved"), Picture::Layout::Rows },
        { QLatin1StringView("columnInterleaved"), Picture::Layout::Columns },
        { QLatin1StringView("frameSequential"), Picture::Layout::FrameSequential },
        { QLatin1StringView("blockLaced"), Picture::Layout::BlockLaced },
        { QLatin1StringView("multiView"), Picture::Layout::MultiView },
    };
    Mark mark;
    if (value == QLatin1String("mono")) {
        mark.known = true;
        return mark;
    }
    for (const Packing &packing : packings) {
        if (value.startsWith(packing.name)) {
            const QStringView order = QStringView(value).mid(packing.name.size());
            if (order == QLatin1String("LeftFirst") || order == QLatin1String("RightFirst")) {
                mark.known = true;
                mark.layout = packing.layout;
                mark.order = order == QLatin1String("RightFirst") ? Picture::EyeOrder::RightFirst : Picture::EyeOrder::LeftFirst;
                return mark;
            }
        }
    }
    // One picture holding both views in colours. Red/cyan glasses carry red before the left eye and green/magenta ones
    // green; JPS names the eye behind the red filter.
    if (value == QLatin1String("anaglyphCyanRed") || value == QLatin1String("anaglyphRedLeft") || value == QLatin1String("anaglyphRedRight")) {
        mark.known = true;
        mark.anaglyph = Picture::AnaglyphColorPair::RedCyan;
        mark.order = value == QLatin1String("anaglyphRedRight") ? Picture::EyeOrder::RightFirst : Picture::EyeOrder::LeftFirst;
    } else if (value == QLatin1String("anaglyphGreenMagenta")) {
        mark.known = true;
        mark.anaglyph = Picture::AnaglyphColorPair::GreenMagenta;
    }
    return mark;
}
}
