// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "picture.h"
#include "panorama_p.h"

#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QtEndian>
#include <algorithm>

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

Picture Picture::load(const QString &path, const Options &options, OutputMode output)
{
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

Picture Picture::fromImages(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Options &options, OutputMode output)
{
    Picture result;
    result.original = image;
    if (image.isNull() || options.view == View::Mono) {
        return result;
    }
    Layout packing = Layout::None;
    EyeOrder order = metadata.endsWith(QLatin1String("RightFirst")) || metadata == QLatin1String("anaglyphRedRight") ? EyeOrder::RightFirst
                                                                                                                     : EyeOrder::LeftFirst;
    Source source = Source::None;
    const bool fallback = format == "jps" && (metadata.isEmpty() || (metadata == QLatin1String("sideBySideRightFirst") && !hasJpsDescriptor(header)));
    if (format == "mpo" && metadata.isEmpty() && !hasStereoMpfIndex(header)) {
        result.source = Source::FileMark;
        return result;
    }
    if (!metadata.isEmpty() && !fallback) {
        source = Source::FileMark;
        if (metadata == QLatin1String("mono")) {
            result.source = Source::FileMark;
            return result;
        }
        if (metadata.startsWith(QLatin1String("sideBySide")))
            packing = Layout::SideBySide;
        else if (metadata.startsWith(QLatin1String("topBottom")))
            packing = Layout::TopAndBottom;
        else if (metadata.startsWith(QLatin1String("multiView")))
            packing = Layout::MultiView;
        else if (metadata.startsWith(QLatin1String("rowInterleaved")))
            packing = Layout::Rows;
        else if (metadata.startsWith(QLatin1String("columnInterleaved")))
            packing = Layout::Columns;
        else if (metadata.startsWith(QLatin1String("checkerboard")))
            packing = Layout::Checkerboard;
        else if (metadata.startsWith(QLatin1String("anaglyphRed")))
            result.anaglyph = AnaglyphColorPair::RedCyan;
        else
            return result;
    } else if (options.inputLayout != Layout::None || options.anaglyph != AnaglyphColorPair::None) {
        packing = options.inputLayout;
        order = options.inputOrder;
        result.anaglyph = options.anaglyph;
        source = Source::User;
    } else if (!second.isNull()) {
        packing = Layout::MultiView;
        order = EyeOrder::LeftFirst;
        source = Source::Format;
    } else if (fallback) {
        packing = Layout::SideBySide;
        order = EyeOrder::RightFirst;
        source = Source::Format;
    } else {
        result.anaglyph = detectAnaglyph(image);
        if (result.anaglyph != AnaglyphColorPair::None) {
            source = Source::Detection;
        } else if (!hasEquirectangularPanoramaMark(header)) {
            if (qint64(image.width()) >= qint64(image.height()) * 2)
                packing = Layout::SideBySide;
            else if (qint64(image.height()) >= qint64(image.width()) * 2)
                packing = Layout::TopAndBottom;
            if (packing != Layout::None)
                source = Source::Shape;
        }
    }
    const auto anaglyph = result.anaglyph;
    if (anaglyph != AnaglyphColorPair::None) {
        // The original anaglyph is already encoded for glasses on this output.
        if (output != OutputMode::Stereo) {
            result.source = source;
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

Picture Picture::fromPackedImage(const QImage &image, Layout layout, EyeOrder order)
{
    Picture result;
    result.layout = layout;
    if (layout == Layout::SideBySide && image.width() % 2 == 0 && image.width() >= 2) {
        const int width = image.width() / 2;
        result.left = image.copy(0, 0, width, image.height());
        result.right = image.copy(width, 0, width, image.height());
    } else if (layout == Layout::TopAndBottom && image.height() % 2 == 0 && image.height() >= 2) {
        const int height = image.height() / 2;
        result.left = image.copy(0, 0, image.width(), height);
        result.right = image.copy(0, height, image.width(), height);
    } else if ((layout == Layout::Rows && image.height() % 2 == 0) || (layout == Layout::Columns && image.width() % 2 == 0)) {
        const QSize size = layout == Layout::Rows ? QSize(image.width(), image.height() / 2) : QSize(image.width() / 2, image.height());
        result.left = QImage(size, QImage::Format_RGBA8888);
        result.right = QImage(size, QImage::Format_RGBA8888);
        for (int y = 0; y < size.height(); ++y) {
            for (int x = 0; x < size.width(); ++x) {
                const QPoint first = layout == Layout::Rows ? QPoint(x, y * 2) : QPoint(x * 2, y);
                const QPoint second = first + (layout == Layout::Rows ? QPoint(0, 1) : QPoint(1, 0));
                result.left.setPixelColor(x, y, image.pixelColor(first));
                result.right.setPixelColor(x, y, image.pixelColor(second));
            }
        }
    } else {
        result.layout = Layout::None;
    }
    result.original = image;
    return order == EyeOrder::RightFirst ? result.swapped() : result;
}

bool Picture::isStereo() const { return layout != Layout::None && !left.isNull() && !right.isNull(); }

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
