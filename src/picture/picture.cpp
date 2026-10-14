// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "picture.h"
#include "mark_p.h"
#include "panorama_p.h"
#include "stereo-policy.h"

#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QXmlStreamReader>
#include <QtEndian>
#include <algorithm>
#include <zlib.h>

namespace Stereo
{
namespace
{
bool hasJpsDescriptor(const QByteArray &data)
{
    if (!data.startsWith(QByteArray::fromHex("ffd8"))) {
        return false;
    }
    qsizetype offset = 2;
    while (offset + 4 <= data.size() && uchar(data[offset]) == 0xff) {
        const uchar marker = uchar(data[offset + 1]);
        if (marker == 0xda || marker == 0xd9) {
            break;
        }
        const auto length = qFromBigEndian<quint16>(data.constData() + offset + 2);
        if (length < 2 || offset + 2 + length > data.size()) {
            break;
        }
        if (marker == 0xe3 && length >= 16 && data.mid(offset + 4, 8) == "_JPSJPS_") {
            return true;
        }
        offset += 2 + length;
    }
    return false;
}

bool hasStereoMpfIndex(const QByteArray &data)
{
    if (!data.startsWith(QByteArray::fromHex("ffd8")))
        return false;
    qsizetype offset = 2;
    while (offset + 4 <= data.size() && uchar(data[offset]) == 0xff) {
        const uchar marker = uchar(data[offset + 1]);
        if (marker == 0xda || marker == 0xd9)
            break;
        const auto length = qFromBigEndian<quint16>(data.constData() + offset + 2);
        if (length < 2 || offset + 2 + length > data.size())
            break;
        if (marker == 0xe2 && length >= 14 && data.mid(offset + 4, 4) == QByteArrayLiteral("MPF\0")) {
            const QByteArrayView tiff(data.constData() + offset + 8, length - 6);
            if (tiff.size() < 8 || (tiff.first(2) != "II" && tiff.first(2) != "MM"))
                return false;
            const bool little = tiff.startsWith("II");
            const auto read16
                = [&](qsizetype at) { return little ? qFromLittleEndian<quint16>(tiff.data() + at) : qFromBigEndian<quint16>(tiff.data() + at); };
            const auto read32
                = [&](qsizetype at) { return little ? qFromLittleEndian<quint32>(tiff.data() + at) : qFromBigEndian<quint32>(tiff.data() + at); };
            const qint64 ifd = read32(4);
            if (read16(2) != 42 || ifd < 8 || ifd + 2 > tiff.size())
                return false;
            const int fields = read16(ifd);
            if (ifd + 2 + qint64(fields) * 12 + 4 > tiff.size())
                return false;
            bool twoImages = false;
            qint64 entries = -1;
            for (qsizetype at = ifd + 2; at < ifd + 2 + fields * 12; at += 12) {
                if (read16(at) == 0xb001 && read16(at + 2) == 4 && read32(at + 4) == 1)
                    twoImages = read32(at + 8) == 2;
                if (read16(at) == 0xb002 && read16(at + 2) == 7 && read32(at + 4) == 32)
                    entries = read32(at + 8);
            }
            const qint64 attributeIfd = read32(ifd + 2 + fields * 12);
            if (attributeIfd != 0) {
                if (attributeIfd < 8 || attributeIfd + 2 > tiff.size())
                    return false;
                const int attributes = read16(attributeIfd);
                if (attributeIfd + 2 + qint64(attributes) * 12 + 4 > tiff.size())
                    return false;
                for (qsizetype at = attributeIfd + 2; at < attributeIfd + 2 + attributes * 12; at += 12) {
                    if (read16(at) == 0xb101 && read16(at + 2) == 4 && read32(at + 4) == 1 && read32(at + 8) == 0)
                        return false;
                }
            }
            return twoImages && entries >= 8 && entries + 32 <= tiff.size() && (read32(entries) & 0x07ffffff) == 0x020002
                && (read32(entries + 16) & 0x07ffffff) == 0x020002;
        }
        offset += 2 + length;
    }
    return false;
}

// The SVGStereo mark: an attribute of the root svg element in a namespace of its own, layout="sideBySideLeftFirst" and the
// other Stereo3DLayout values, with the viewBox holding both views. SVG renderers ignore attributes from other namespaces
// (SVG 1.1 Second Edition, 23.1 "Foreign namespaces and private data"), so a marked file draws as before everywhere else.
constexpr QLatin1StringView svgStereoNamespace("https://github.com/Sparky-OS/svgstereo");

// The start of a gzip-compressed SVG (RFC 1952), inflated as far as the bytes at hand and the limit allow: the root element
// is near the start of the document.
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

struct Decision {
    Picture::Layout packing = Picture::Layout::None;
    Picture::EyeOrder order = Picture::EyeOrder::LeftFirst;
    Picture::Source source = Picture::Source::None;
    Picture::AnaglyphColorPair anaglyph = Picture::AnaglyphColorPair::None;
    // Only the pixels can tell: anaglyph detection first, then the shape unless a panorama mark vetoes it.
    bool pixels = false;
    bool panorama = false;
};

// What the file's marks, its format and the user's choice say, before any pixel is read.
Decision decide(const QString &fileMetadata, const QByteArray &format, const QByteArray &header, bool second, const Picture::Options &options)
{
    Decision result;
    const bool drawing = format == "svg" || format == "svgz";
    QString metadata = fileMetadata;
    if (metadata.isEmpty() && format == "svg")
        metadata = svgStereoLayout(header);
    else if (metadata.isEmpty() && format == "svgz")
        metadata = svgStereoLayout(gunzipHeader(header, 1024 * 1024));
    result.order = metadata.endsWith(QLatin1String("RightFirst")) || metadata == QLatin1String("anaglyphRedRight") ? Picture::EyeOrder::RightFirst
                                                                                                                   : Picture::EyeOrder::LeftFirst;
    const bool fallback = format == "jps" && (metadata.isEmpty() || (metadata == QLatin1String("sideBySideRightFirst") && !hasJpsDescriptor(header)));
    if (format == "mpo" && metadata.isEmpty() && !hasStereoMpfIndex(header)) {
        result.source = Picture::Source::FileMark;
        return result;
    }
    if (!metadata.isEmpty() && !fallback) {
        result.source = Picture::Source::FileMark;
        const Mark mark = readMark(metadata);
        // A still is one moment: views that alternate in time or in a stream's blocks are not in it.
        if (mark.layout != Picture::Layout::FrameSequential && mark.layout != Picture::Layout::BlockLaced) {
            result.packing = mark.layout;
            result.order = mark.order;
            result.anaglyph = mark.anaglyph;
        }
        return result;
    }
    if (options.inputLayout != Picture::Layout::None || options.anaglyph != Picture::AnaglyphColorPair::None) {
        result.packing = options.inputLayout;
        result.order = options.inputOrder;
        result.anaglyph = options.anaglyph;
        result.source = Picture::Source::User;
    } else if (second) {
        result.packing = Picture::Layout::MultiView;
        result.order = Picture::EyeOrder::LeftFirst;
        result.source = Picture::Source::Format;
    } else if (fallback) {
        result.packing = Picture::Layout::SideBySide;
        result.order = Picture::EyeOrder::RightFirst;
        result.source = Picture::Source::Format;
    } else if (!drawing) {
        // A drawing is one picture unless it says otherwise: neither its colours nor its shape make it a pair.
        result.pixels = true;
        result.panorama = hasEquirectangularPanoramaMark(header);
    }
    return result;
}

// A stereo photograph is two pictures next to each other: from two squares (2:1) to two 16:9 pictures (32:9, 3.56:1),
// with a little margin. A wider picture is a panorama, and a tall one a portrait or a phone screenshot: the shape alone
// never says top and bottom.
bool pairByShape(const QSize &size) { return stereo_policy_picture_pair_by_shape(size.width(), size.height()) == 1; }

Picture unpackPng(const QImage &image, Picture::EyeOrder order)
{
    const int padding = 15 - ((image.width() - 1) % 16);
    if (padding > 7 || image.width() <= padding) {
        return {};
    }
    const int width = (image.width() - padding) / 2;
    Picture result;
    result.left = image.copy(0, 0, width, image.height());
    result.right = image.copy(width + padding, 0, width, image.height());
    result.layout = Picture::Layout::SideBySide;
    return order == Picture::EyeOrder::RightFirst ? result.swapped() : result;
}
}

bool Picture::userSwapEyes(const QString &path) { return stereo_policy_file_swap_eyes(QFile::encodeName(path).constData()) == 1; }

bool Picture::setUserSwapEyes(const QString &path, bool swap)
{
    return stereo_policy_set_file_swap_eyes(QFile::encodeName(path).constData(), swap) == 0;
}

bool Picture::canStoreUserSwapEyes(const QString &path) { return stereo_policy_file_swap_eyes(QFile::encodeName(path).constData()) >= 0; }

Picture Picture::load(const QString &path, const Options &fileOptions, OutputMode output)
{
    // the user's correction stored with the file, over its mark
    Options options = fileOptions;
    options.swapEyes = options.swapEyes != userSwapEyes(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray header = file.peek(1024 * 1024);
    const QByteArray extension = QFileInfo(path).suffix().toLatin1().toLower();
    QImageReader reader(&file, extension == "jps" || extension == "mpo" ? extension : QByteArray());
    QString metadata = reader.text(QStringLiteral("Stereo3DLayout"));
    const QByteArray detectedFormat = reader.format();
    const QByteArray format = extension == "jps" || extension == "mpo" || extension == "pns" ? extension : detectedFormat;
    const bool multiView = reader.imageCount() >= 2 && (format == "mpo" || metadata.startsWith(QLatin1String("multiView")));
    const QImage image = reader.read();
    if (metadata.isEmpty()) {
        metadata = image.text(QStringLiteral("Stereo3DLayout"));
    }
    QImage second;
    if (multiView && reader.jumpToImage(1)) {
        second = reader.read();
    }
    return fromImages(image, second, metadata, format, header, options, output);
}

Picture Picture::loadPair(const QString &path, const QSize &bound, const Options &fileOptions)
{
    Options options = fileOptions;
    options.swapEyes = options.swapEyes != userSwapEyes(path);
    QFile file(path);
    if (bound.isEmpty() || options.view == View::Mono || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray header = file.peek(1024 * 1024);
    const QByteArray extension = QFileInfo(path).suffix().toLatin1().toLower();
    QImageReader reader(&file, extension == "jps" || extension == "mpo" ? extension : QByteArray());
    QString metadata = reader.text(QStringLiteral("Stereo3DLayout"));
    const QByteArray format = extension == "jps" || extension == "mpo" || extension == "pns" ? extension : reader.format();
    const bool multiView = reader.imageCount() >= 2 && (format == "mpo" || metadata.startsWith(QLatin1String("multiView")));
    const QSize size = reader.size();
    const Decision decision = decide(metadata, format, header, multiView, options);
    if (decision.pixels ? decision.panorama || (size.isValid() && !pairByShape(size)) : decision.packing == Layout::None) {
        return {};
    }
    // A packed picture holds two views in either direction, so each image is decoded to fit twice the bound. Interleaved,
    // checkerboard and quincunx views are taken from the picture's own pixels, so those are decoded at their size.
    const QSize limit = bound * 2;
    const bool interleaved = decision.packing == Layout::Rows || decision.packing == Layout::Columns || decision.packing == Layout::Checkerboard
        || decision.packing == Layout::SideBySideQuincunx;
    const auto read = [&reader, &limit, interleaved](const QSize &size) {
        if (!interleaved && size.isValid() && reader.supportsOption(QImageIOHandler::ScaledSize)
            && (size.width() > limit.width() || size.height() > limit.height() || reader.format() == "svg" || reader.format() == "svgz")) {
            QSize scaled = size.scaled(limit, Qt::KeepAspectRatio);
            scaled = QSize(qMax(2, scaled.width() & ~1), qMax(2, scaled.height() & ~1));
            reader.setScaledSize(scaled);
        }
        return reader.read();
    };
    const QImage image = read(size);
    if (metadata.isEmpty()) {
        metadata = image.text(QStringLiteral("Stereo3DLayout"));
    }
    QImage second;
    if (multiView && reader.jumpToImage(1)) {
        second = read(reader.size());
    }
    Picture result = fromImages(image, second, metadata, format, header, options, OutputMode::Unknown);
    if (!result.isStereo()) {
        return {};
    }
    const QSize eye = result.left.size().scaled(QSize(bound.width() / 2, bound.height()), Qt::KeepAspectRatio);
    if (result.left.width() > eye.width() || result.left.height() > eye.height() || result.right.size() != result.left.size()) {
        result.left = result.left.scaled(eye, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        result.right = result.right.scaled(eye, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return result;
}

Picture Picture::fromImages(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Options &options, OutputMode output)
{
    Picture result;
    result.original = image;
    if (image.isNull() || options.view == View::Mono) {
        return result;
    }
    Decision decision = decide(metadata, format, header, !second.isNull(), options);
    if (decision.pixels) {
        decision.anaglyph = detectAnaglyph(image);
        if (decision.anaglyph != AnaglyphColorPair::None) {
            decision.source = Source::Detection;
        } else if (!decision.panorama && pairByShape(image.size())) {
            decision.packing = Layout::SideBySide;
            decision.source = Source::Shape;
        }
    }
    if (decision.packing == Layout::None && decision.anaglyph == AnaglyphColorPair::None) {
        result.source = decision.source;
        return result;
    }
    const Layout packing = decision.packing;
    const EyeOrder order = decision.order;
    const Source source = decision.source;
    const auto anaglyph = decision.anaglyph;
    if (anaglyph != AnaglyphColorPair::None) {
        // The original anaglyph is already encoded for glasses on this output.
        if (output != OutputMode::Stereo) {
            result.source = source;
            result.anaglyph = anaglyph;
            return result;
        }
        result = fromAnaglyph(image, anaglyph);
        if (order == EyeOrder::RightFirst)
            result = result.swapped();
    } else if (packing == Layout::MultiView) {
        result.left = image;
        result.right = second;
        result.layout = packing;
        if (order == EyeOrder::RightFirst)
            result = result.swapped();
    } else if (source == Source::FileMark && (format == "png" || format == "pns") && packing == Layout::SideBySide) {
        result = unpackPng(image, order);
    } else {
        result = fromPackedImage(image, packing, order);
    }
    result.original = image;
    result.source = source;
    result.anaglyph = anaglyph;
    if (options.swapEyes)
        result = result.swapped();
    if (result.isStereo() && (options.view == View::LeftOnly || options.view == View::RightOnly)) {
        result.original = options.view == View::LeftOnly ? result.left : result.right;
        result.left = {};
        result.right = {};
        result.layout = Layout::None;
    }
    return result;
}

namespace
{
// One view of an interleaved, checkerboard or quincunx-sampled picture at the size of the whole frame, as a display
// upconverts it (H.264 and H.265 Table D-8): the samples of the view stay where they are, and each missing one is the
// mean of its two neighbours across the interleaving. phase is the first row, column or checker of the view. A quincunx
// half holds the samples of a full-width view taken on even columns of even rows and odd columns of odd rows, both views
// alike (FFmpeg's AV_STEREO3D_SIDEBYSIDE_QUINCUNX).
QImage upconverted(const QImage &frame, Picture::Layout layout, int phase)
{
    const QImage source = frame.convertToFormat(QImage::Format_RGBA8888);
    const bool quincunx = layout == Picture::Layout::SideBySideQuincunx;
    const int width = quincunx ? source.width() * 2 : source.width();
    const int height = source.height();
    const auto own = [layout, phase](int x, int y) {
        if (layout == Picture::Layout::Rows)
            return y % 2 == phase;
        if (layout == Picture::Layout::Columns)
            return x % 2 == phase;
        return (x + y) % 2 == phase;
    };
    const auto sample = [&source, quincunx](int x, int y) { return source.constScanLine(y) + 4 * (quincunx ? x / 2 : x); };
    QImage view(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        uchar *out = view.scanLine(y);
        for (int x = 0; x < width; ++x) {
            if (own(x, y)) {
                std::copy_n(sample(x, y), 4, out + 4 * x);
                continue;
            }
            const bool rows = layout == Picture::Layout::Rows;
            const QPoint before = rows ? QPoint(x, y - 1) : QPoint(x - 1, y);
            const QPoint after = rows ? QPoint(x, y + 1) : QPoint(x + 1, y);
            const bool hasBefore = before.x() >= 0 && before.y() >= 0;
            const bool hasAfter = after.x() < width && after.y() < height;
            const uchar *first = sample(hasBefore ? before.x() : after.x(), hasBefore ? before.y() : after.y());
            const uchar *second = sample(hasAfter ? after.x() : before.x(), hasAfter ? after.y() : before.y());
            for (int channel = 0; channel < 4; ++channel)
                out[4 * x + channel] = uchar((first[channel] + second[channel] + 1) / 2);
        }
    }
    return view;
}
}

Picture Picture::fromPackedImage(const QImage &image, Layout layout, EyeOrder order)
{
    Picture result;
    result.layout = layout;
    if (layout == Layout::SideBySideQuincunx && image.width() % 2 == 0 && image.width() >= 2) {
        const int width = image.width() / 2;
        result.left = upconverted(image.copy(0, 0, width, image.height()), layout, 0);
        result.right = upconverted(image.copy(width, 0, width, image.height()), layout, 0);
    } else if ((layout == Layout::Rows && image.height() >= 2) || (layout == Layout::Columns && image.width() >= 2)
        || (layout == Layout::Checkerboard && image.width() >= 2)) {
        result.left = upconverted(image, layout, 0);
        result.right = upconverted(image, layout, 1);
    } else if (layout == Layout::SideBySide && image.width() % 2 == 0 && image.width() >= 2) {
        const int width = image.width() / 2;
        result.left = image.copy(0, 0, width, image.height());
        result.right = image.copy(width, 0, width, image.height());
    } else if (layout == Layout::TopAndBottom && image.height() % 2 == 0 && image.height() >= 2) {
        const int height = image.height() / 2;
        result.left = image.copy(0, 0, image.width(), height);
        result.right = image.copy(0, height, image.width(), height);
    } else {
        result.layout = Layout::None;
    }
    result.original = image;
    return order == EyeOrder::RightFirst ? result.swapped() : result;
}

bool Picture::isStereo() const { return layout != Layout::None && !left.isNull() && !right.isNull(); }

QImage Picture::sideBySide() const
{
    if (!isStereo()) {
        return {};
    }
    const bool alpha = left.hasAlphaChannel() || right.hasAlphaChannel();
    QImage image(
        left.width() + right.width(), qMax(left.height(), right.height()), alpha ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, left);
    painter.drawImage(left.width(), 0, right);
    painter.end();
    image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
    return image;
}

Picture Picture::swapped() const
{
    Picture result = *this;
    std::swap(result.left, result.right);
    result.eyeOrder = eyeOrder == EyeOrder::LeftFirst ? EyeOrder::RightFirst : EyeOrder::LeftFirst;
    return result;
}

QString Picture::colorDisclaimer()
{
    return QObject::tr(
        "Colour is reconstructed and approximate: an anaglyph keeps only some of each eye's colour. The original file is never changed.");
}
}
