// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include "picture.h"

namespace Stereo
{
// A Stereo3DLayout value: the edition's words for every stereo layout of a picture or a video (RFC 9559 StereoMode,
// section 5.1.4.1.28.3; H.264 and H.265 Table D-8; the JPS and MPO marks; SVGStereo), read exactly. A value outside
// the set is not known, and says one picture. H.264 V15 (08/2024) numbers the frame packing table D-8, V16 (06/2026) D-9.
struct Mark {
    bool known = false;
    Picture::Layout layout = Picture::Layout::None;
    Picture::EyeOrder order = Picture::EyeOrder::LeftFirst;
    Picture::AnaglyphColorPair anaglyph = Picture::AnaglyphColorPair::None;
};
Mark readMark(const QString &value);
// The SVGStereo mark of a drawing (its root svg element's layout attribute, SVGStereo 0.4): the value, empty when the
// root has none. isSvgDocument() says whether the bytes start an SVG document at all; gunzipHeader() inflates the start
// of an SVGZ file (RFC 1952) as far as the bytes at hand and limit allow.
QString svgStereoLayout(const QByteArray &header);
bool isSvgDocument(const QByteArray &header);
QByteArray gunzipHeader(const QByteArray &data, qsizetype limit);
}
