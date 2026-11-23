// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "mark_p.h"
#include "stereo-policy.h"

#include <QXmlStreamReader>
#include <zlib.h>

namespace Stereo
{
Mark readMark(const QString &value)
{
    int layout = 0;
    int order = 0;
    int anaglyph = 0;
    Mark mark;
    mark.known = stereo_policy_read_mark(value.toUtf8().constData(), &layout, &order, &anaglyph) == 1;
    mark.layout = Picture::Layout(layout);
    mark.order = Picture::EyeOrder(order);
    mark.anaglyph = Picture::AnaglyphColorPair(anaglyph);
    return mark;
}

// The SVGStereo mark: an attribute of the root svg element in a namespace of its own, layout="sideBySideLeftFirst" and the
// other Stereo3DLayout values, with the viewBox holding both views. SVG renderers ignore attributes from other namespaces
// (SVG 1.1 Second Edition, 23.1 "Foreign namespaces and private data"), so a marked file draws as before everywhere else.
namespace
{
constexpr QLatin1StringView svgStereoNamespace("https://github.com/Sparky-OS/svgstereo");

// The start of a gzip-compressed SVG (RFC 1952), inflated as far as the bytes at hand and the limit allow: the root element
// is near the start of the document.
}

QByteArray gunzipHeader(const QByteArray &data, qsizetype limit)
{
    z_stream stream = {};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        return {};
    QByteArray result(limit, Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(result.data());
    stream.avail_out = uInt(limit);
    const int status = inflate(&stream, Z_SYNC_FLUSH);
    result.truncate(status == Z_OK || status == Z_STREAM_END || status == Z_BUF_ERROR ? qsizetype(stream.total_out) : 0);
    inflateEnd(&stream);
    return result;
}

QString svgStereoLayout(const QByteArray &header)
{
    QXmlStreamReader xml(header);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            if (xml.name() != QLatin1String("svg") || xml.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg")) {
                return {};
            }
            return xml.attributes().value(svgStereoNamespace, QLatin1String("layout")).toString();
        }
    }
    return {};
}

bool isSvgDocument(const QByteArray &header)
{
    QXmlStreamReader xml(header);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            return xml.name() == QLatin1String("svg") && xml.namespaceUri() == QLatin1String("http://www.w3.org/2000/svg");
        }
    }
    return false;
}
}
