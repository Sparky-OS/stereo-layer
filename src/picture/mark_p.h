// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include "picture.h"

namespace Stereo
{
// A Stereo3DLayout value: the edition's words for every stereo layout of a picture or a video (RFC 9559 StereoMode,
// section 5.1.4.1.28.3; H.264 and H.265 Table D-8; the JPS and MPO marks; SVGStereo), read exactly. A value outside
// the set is not known, and says one picture.
struct Mark {
    bool known = false;
    Picture::Layout layout = Picture::Layout::None;
    Picture::EyeOrder order = Picture::EyeOrder::LeftFirst;
    Picture::AnaglyphColorPair anaglyph = Picture::AnaglyphColorPair::None;
};
Mark readMark(const QString &value);
}
