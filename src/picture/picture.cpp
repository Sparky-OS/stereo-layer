// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "picture.h"
#include "mark_p.h"
#include "panorama_p.h"
#include "stereo-dibr.h"
#include "stereo-policy.h"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPainter>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtEndian>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <vector>
#include <zlib.h>

namespace Stereo
{
Q_LOGGING_CATEGORY(qLcPicture, "org.sparky.stereo.picture")
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
        if (marker == 0xff) {
            ++offset;
            continue;
        }
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
Decision decide(const QString &fileMetadata, const QByteArray &format, const QByteArray &header, bool second, const Picture::Options &options,
    bool overMark = false)
{
    Decision result;
    // The person's packing for this one file (Picture::setFileChoice()) corrects it, whatever the file declares.
    if (overMark && (options.inputLayout != Picture::Layout::None || options.anaglyph != Picture::AnaglyphColorPair::None)) {
        result.packing = options.inputLayout;
        result.order = options.inputOrder;
        result.anaglyph = options.anaglyph;
        result.source = Picture::Source::User;
        return result;
    }
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
        // The order of a still: the user's choice, the file's marks, then (a call point of its own, the file-name tag
        // reader of libstereo-policy goes here), then the pixels: a shape that may be a pair, and halves that are
        // views of one scene (fromImages()).
        result.pixels = true;
        result.panorama = hasEquirectangularPanoramaMark(header);
    }
    return result;
}

// A stereo photograph is two pictures next to each other: from two squares (2:1) to two 16:9 pictures (32:9, 3.56:1),
// with a little margin. A wider picture is a panorama, and a tall one a portrait or a phone screenshot: the shape never
// says top and bottom. Inside that band, a picture without a mark is a side-by-side pair: the last safety net for files that
// say nothing (Daniel, 2026-10-13), and the pixels are not asked. A pair with a shallow or no depth, a sunk texture say, has
// halves a comparison cannot tell from two unrelated ones, and a comparison that says "not a pair" leaves a real pair flat
// (ORDER 504). Picture::pairByContent() stays for a program that wants the comparison itself.
bool pairByShape(const QSize &size) { return stereo_policy_picture_pair_by_shape(size.width(), size.height()) == 1; }

// Why an unmarked picture is shown as a pair or flat, for whoever asks the log (QT_LOGGING_RULES="org.sparky.stereo.picture=true").
void logShape(const QSize &size, bool pair, bool panorama)
{
    if (!qLcPicture().isDebugEnabled() || size.height() <= 0) {
        return;
    }
    const double ratio = double(size.width()) / size.height();
    if (panorama) {
        qCDebug(qLcPicture).noquote() << "unmarked picture" << size << "is shown flat: its XMP says an equirectangular panorama (Photo Sphere)";
    } else if (pair) {
        qCDebug(qLcPicture).noquote() << "unmarked picture" << size << "is a side by side pair by its shape:" << ratio
                                      << ": 1 is between 2:1 and 3.6:1";
    } else {
        qCDebug(qLcPicture).noquote() << "unmarked picture" << size << "is shown flat: no mark, and its shape" << ratio
                                      << ": 1 is outside 2:1 to 3.6:1";
    }
}

// The width at which the halves of a picture are compared (Picture::pairByContent()).
constexpr int pairSampleWidth = 384;

Picture unpackWithDepth(const QImage &image, Picture::Layout layout, Picture::EyeOrder order, const Picture::DepthOptions &depthOptions);

Picture fromImagesComparing(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Picture::Options &options, OutputMode output, const Picture::DepthOptions &depthOptions = {}, const QImage &depthPicture = {},
    bool overMark = false);

// A HEIF stem group may keep a separate monoscopic primary at image zero.
// The reader names the actual stereo view image numbers without changing that primary.
std::optional<QPair<int, int>> stereoViewIndices(QImageReader &reader)
{
    const QStringList fields = reader.text(QStringLiteral("Stereo3DViewIndices")).split(QLatin1Char(','));
    if (fields.size() != 2) {
        return std::nullopt;
    }
    bool firstOk = false, secondOk = false;
    const int first = fields[0].toInt(&firstOk), second = fields[1].toInt(&secondOk);
    if (!firstOk || !secondOk || first < 0 || second < 0 || first == second || first >= reader.imageCount() || second >= reader.imageCount()) {
        return std::nullopt;
    }
    return QPair(first, second);
}

// Whether a reader's second image is a view of the pair (Multi-Picture Format, multiView marks) or the depth map
// (depthSeparate), so that load() and loadPair() read it.
bool hasSecondImage(int imageCount, const QByteArray &format, const QString &metadata)
{
    return imageCount >= 2 && (format == "mpo" || metadata.startsWith(QLatin1String("multiView")) || metadata == QLatin1String("depthSeparate"));
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

bool Picture::pairByContent(const QImage &image)
{
    if (image.isNull())
        return false;
    // The function reduces a larger picture itself; a copy of a few hundred pixels keeps a thumbnailer fast.
    const QImage small = (image.width() > pairSampleWidth ? image.scaledToWidth(pairSampleWidth, Qt::SmoothTransformation) : image)
                             .convertToFormat(QImage::Format_RGB888);
    return stereo_policy_picture_pair_by_content(small.constBits(), small.width(), small.height(), small.bytesPerLine()) == 1;
}

bool Picture::userSwapEyes(const QString &path) { return stereo_policy_file_swap_eyes(QFile::encodeName(path).constData()) == 1; }

bool Picture::setUserSwapEyes(const QString &path, bool swap)
{
    return stereo_policy_set_file_swap_eyes(QFile::encodeName(path).constData(), swap) == 0;
}

bool Picture::canStoreUserSwapEyes(const QString &path) { return stereo_policy_file_swap_eyes(QFile::encodeName(path).constData()) >= 0; }

namespace
{
Picture::Options storedChoice(const QString &path, bool *found = nullptr)
{
    struct stereo_policy_file_choice stored;
    const bool has = stereo_policy_file_choice(QFile::encodeName(path).constData(), &stored) == 1;
    if (found) {
        *found = has;
    }
    Picture::Options choice;
    if (has) {
        choice.inputLayout = Picture::Layout(stored.layout);
        choice.inputOrder = stored.order == STEREO_POLICY_RIGHT_FIRST ? Picture::EyeOrder::RightFirst : Picture::EyeOrder::LeftFirst;
        choice.anaglyph = Picture::AnaglyphColorPair(stored.anaglyph);
        choice.view = Picture::View(stored.view);
        choice.swapEyes = stored.swap_eyes == 1;
    }
    return choice;
}

// The caller's options with the file's stored choice: the caller's own fields win, the swaps add up.
Picture::Options withFileChoice(const QString &path, const Picture::Options &caller, bool *overMark)
{
    Picture::Options options = caller;
    const Picture::Options stored = storedChoice(path);
    options.swapEyes = (caller.swapEyes != stored.swapEyes) != Picture::userSwapEyes(path);
    if (caller.inputLayout == Picture::Layout::None && caller.anaglyph == Picture::AnaglyphColorPair::None
        && (stored.inputLayout != Picture::Layout::None || stored.anaglyph != Picture::AnaglyphColorPair::None)) {
        options.inputLayout = stored.inputLayout;
        options.inputOrder = stored.inputOrder;
        options.anaglyph = stored.anaglyph;
        *overMark = true;
    }
    // a packing the caller chose itself (a viewer's menu, for this session) is shown: a stored "Not 3D" does not veto it
    if (caller.view == Picture::View::Stereo && caller.inputLayout == Picture::Layout::None && caller.anaglyph == Picture::AnaglyphColorPair::None) {
        options.view = stored.view;
    }
    return options;
}
}

Picture::Options Picture::fileChoice(const QString &path)
{
    Options choice = storedChoice(path);
    choice.swapEyes = choice.swapEyes != userSwapEyes(path);
    return choice;
}

bool Picture::hasFileChoice(const QString &path)
{
    const Options choice = fileChoice(path);
    return choice.inputLayout != Layout::None || choice.anaglyph != AnaglyphColorPair::None || choice.view != View::Stereo || choice.swapEyes;
}

bool Picture::setFileChoice(const QString &path, const Options &choice, QString *error)
{
    const QByteArray name = QFile::encodeName(path);
    // A swap an earlier version kept in the file's attribute user.stereo-policy.swap-eyes moves into the store with
    // this choice. Only stereo_policy_set_file_swap_eyes() ever wrote that name, at a person's Swap Eyes, so it is
    // taken out of the file; where the file cannot be changed it stays and the stored swap is kept against it.
    bool legacy = userSwapEyes(path);
    if (legacy && stereo_policy_set_file_swap_eyes(name.constData(), 0) == 0) {
        legacy = false;
    }
    struct stereo_policy_file_choice stored;
    stored.layout = int(choice.inputLayout);
    stored.order = choice.inputOrder == EyeOrder::RightFirst ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
    stored.anaglyph = int(choice.anaglyph);
    stored.view = int(choice.view);
    stored.swap_eyes = choice.swapEyes != legacy;
    if (stereo_policy_set_file_choice(name.constData(), &stored) != 0) {
        const int code = errno;
        if (error) {
            char entry[4096] = "";
            stereo_policy_file_choice_entry(name.constData(), entry, sizeof(entry));
            *error = QCoreApplication::translate("Stereo::Picture", "The choice for this file cannot be kept in %1: %2.")
                         .arg(QFile::decodeName(entry), QString::fromLocal8Bit(strerror(code)));
        }
        return false;
    }
    dropThumbnails(path);
    return true;
}

bool Picture::adoptSwapEyes(const QString &path, bool swap, QString *error)
{
    const QByteArray name = QFile::encodeName(path);
    struct stereo_policy_file_choice stored;
    const int has = stereo_policy_file_choice(name.constData(), &stored);
    if (has == 1) {
        return true;
    }
    stored = {0, 0, 0, 0, swap != userSwapEyes(path)};
    if (has < 0 || stereo_policy_set_file_choice(name.constData(), &stored) != 0) {
        const int code = errno;
        if (error) {
            char entry[4096] = "";
            stereo_policy_file_choice_entry(name.constData(), entry, sizeof(entry));
            *error = QCoreApplication::translate("Stereo::Picture", "The choice for this file cannot be kept in %1: %2.")
                         .arg(QFile::decodeName(entry), QString::fromLocal8Bit(strerror(code)));
        }
        return false;
    }
    dropThumbnails(path);
    return true;
}

Picture::SaveResult Picture::saveFileChoice(const QString &path)
{
    bool found = false;
    storedChoice(path, &found);
    if (!found && !userSwapEyes(path)) {
        return {false, QCoreApplication::translate("Stereo::Picture", "This file has no 3D correction to save.")};
    }
    // The shared save rewrites the descriptor of a JPEG, JPS or MPO file; say so plainly for any other format instead of
    // a JPEG parser's message.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.read(2) != QByteArray("\xff\xd8", 2)) {
        return {false,
            QCoreApplication::translate("Stereo::Picture", "A correction can be saved into JPEG, JPS and MPO pictures only; this file has another format.")};
    }
    file.close();
    // the shared save takes the stored choice itself and clears it when the file carries it
    return saveCorrection(path, {});
}

QStringList Picture::moveFileChoice(const QString &from, const QString &to, QString *error, bool *failed)
{
    QStringList carried;
    const auto note = [](const char *path, void *list) {
        static_cast<QStringList *>(list)->append(QFile::decodeName(path));
    };
    const int moved = stereo_policy_move_file_choice(QFile::encodeName(from).constData(), QFile::encodeName(to).constData(), note, &carried);
    if (failed) {
        *failed = moved < 0;
    }
    if (moved < 0 && error) {
        *error = QCoreApplication::translate("Stereo::Picture", "The 3D choice of this file could not follow it to %1: %2.")
                     .arg(to, QString::fromLocal8Bit(strerror(errno)));
    }
    for (const QString &path : std::as_const(carried)) {
        dropThumbnails(path);
    }
    return carried;
}

void Picture::dropThumbnails(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/thumbnails/");
    QSet<QString> names;
    for (const QString &file : {path, canonical}) {
        if (file.isEmpty()) {
            continue;
        }
        // the URI as the desktop's thumbnailers write it (KIO::PreviewJob: the file's URL, QUrl::toEncoded(QUrl::RemovePassword | QUrl::FullyEncoded))
        const QUrl url = QUrl::fromLocalFile(QDir::cleanPath(QFileInfo(file).absoluteFilePath()));
        names.insert(QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(QUrl::RemovePassword | QUrl::FullyEncoded), QCryptographicHash::Md5).toHex()) + QLatin1String(".png"));
    }
    const QStringList sizes = QDir(cache).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &size : sizes) {
        for (const QString &name : std::as_const(names)) {
            QFile::remove(cache + size + QLatin1Char('/') + name);
        }
    }
}

Picture Picture::load(const QString &path, const Options &fileOptions, OutputMode output)
{
    return load(path, fileOptions, DepthOptions(), DepthPicture::Shown, output);
}

Picture Picture::load(const QString &path, const Options &fileOptions, const DepthOptions &depthOptions, DepthPicture depthPicture, OutputMode output)
{
    // the person's choice for this file, from the per-file store, with the caller's own
    bool overMark = false;
    const Options options = withFileChoice(path, fileOptions, &overMark);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray header = file.peek(1024 * 1024);
    const QByteArray extension = QFileInfo(path).suffix().toLatin1().toLower();
    QImageReader reader(&file,
        extension == "mpo"             ? extension
            : hasJpsDescriptor(header) ? QByteArray("jps")
            : extension == "jps"       ? extension
                                       : QByteArray());
    QString metadata = reader.text(QStringLiteral("Stereo3DLayout"));
    const QByteArray detectedFormat = reader.format();
    const QByteArray format = extension == "jps" || extension == "mpo" || extension == "pns" ? extension : detectedFormat;
    const int imageCount = reader.imageCount();
    QImage image = reader.read();
    const QImage shown = image;
    if (metadata.isEmpty()) {
        metadata = image.text(QStringLiteral("Stereo3DLayout"));
    }
    QImage second;
    const bool indexed = metadata.startsWith(QLatin1String("multiView")) && !reader.text(QStringLiteral("Stereo3DViewIndices")).isEmpty()
        && options.view != View::Mono;
    if (indexed) {
        const auto indices = stereoViewIndices(reader);
        if (!indices || !reader.jumpToImage(indices->first)) {
            Picture result;
            result.original = shown;
            return result;
        }
        image = reader.read();
        if (reader.jumpToImage(indices->second)) {
            second = reader.read();
        }
    } else if (hasSecondImage(imageCount, format, metadata) && reader.jumpToImage(1)) {
        second = reader.read();
    }
    QImage original;
    if (metadata == QLatin1String("depthSeparate") && depthPicture == DepthPicture::Original && imageCount >= 3 && reader.jumpToImage(2)) {
        original = reader.read();
    }
    Picture result = fromImagesComparing(image, second, metadata, format, header, options, output, depthOptions, original, overMark);
    if (indexed && options.view == View::Stereo) {
        result.original = shown;
    }
    return result;
}

Picture Picture::loadPair(const QString &path, const QSize &bound, const Options &fileOptions)
{
    return loadPair(path, bound, fileOptions, DepthOptions(), DepthPicture::Shown);
}

Picture Picture::loadPair(
    const QString &path, const QSize &bound, const Options &fileOptions, const DepthOptions &depthOptions, DepthPicture depthPicture)
{
    bool overMark = false;
    const Options options = withFileChoice(path, fileOptions, &overMark);
    QFile file(path);
    if (bound.isEmpty() || options.view == View::Mono || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray header = file.peek(1024 * 1024);
    const QByteArray extension = QFileInfo(path).suffix().toLatin1().toLower();
    QImageReader reader(&file,
        extension == "mpo"             ? extension
            : hasJpsDescriptor(header) ? QByteArray("jps")
            : extension == "jps"       ? extension
                                       : QByteArray());
    QString metadata = reader.text(QStringLiteral("Stereo3DLayout"));
    const QByteArray format = extension == "jps" || extension == "mpo" || extension == "pns" ? extension : reader.format();
    const int imageCount = reader.imageCount();
    const bool multiView = hasSecondImage(imageCount, format, metadata);
    const QSize size = reader.size();
    const Decision decision = decide(metadata, format, header, multiView, options, overMark);
    if (decision.pixels ? decision.panorama || (size.isValid() && !pairByShape(size)) : decision.packing == Layout::None) {
        if (decision.pixels) {
            logShape(size, false, decision.panorama);
        }
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
    QImage image = read(size);
    const QImage shown = image;
    if (metadata.isEmpty()) {
        metadata = image.text(QStringLiteral("Stereo3DLayout"));
    }
    QImage second;
    const bool indexed = metadata.startsWith(QLatin1String("multiView")) && !reader.text(QStringLiteral("Stereo3DViewIndices")).isEmpty();
    if (indexed) {
        const auto indices = stereoViewIndices(reader);
        if (!indices || !reader.jumpToImage(indices->first)) {
            return {};
        }
        image = read(reader.size());
        if (reader.jumpToImage(indices->second)) {
            second = read(reader.size());
        }
    } else if (multiView && reader.jumpToImage(1)) {
        second = read(reader.size());
    }
    QImage original;
    if (metadata == QLatin1String("depthSeparate") && depthPicture == DepthPicture::Original && imageCount >= 3 && reader.jumpToImage(2)) {
        original = read(reader.size());
    }
    Picture result = fromImagesComparing(image, second, metadata, format, header, options, OutputMode::Unknown, depthOptions, original, overMark);
    if (indexed && options.view == View::Stereo) {
        result.original = shown;
    }
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

namespace
{
// fromImages(), with the depth options
Picture fromImagesComparing(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Picture::Options &options, OutputMode output, const Picture::DepthOptions &depthOptions, const QImage &depthPicture, bool overMark)
{
    using Layout = Picture::Layout;
    using EyeOrder = Picture::EyeOrder;
    using Source = Picture::Source;
    using View = Picture::View;
    using AnaglyphColorPair = Picture::AnaglyphColorPair;
    Picture result;
    result.original = image;
    if (image.isNull() || options.view == View::Mono) {
        return result;
    }
    Decision decision = decide(metadata, format, header, !second.isNull(), options, overMark);
    if (decision.pixels) {
        decision.anaglyph = Picture::detectAnaglyph(image);
        if (decision.anaglyph != AnaglyphColorPair::None) {
            decision.source = Source::Detection;
        } else if (!decision.panorama && pairByShape(image.size())) {
            decision.packing = Layout::SideBySide;
            decision.source = Source::Shape;
            logShape(image.size(), true, false);
        } else {
            logShape(image.size(), false, decision.panorama);
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
        result = Picture::fromAnaglyph(image, anaglyph);
        if (order == EyeOrder::RightFirst)
            result = result.swapped();
    } else if (packing == Layout::DepthSeparate) {
        // the depth map is the file's second image
        const QString depthMark = image.text(QStringLiteral("Stereo3DDepth")).isEmpty() ? second.text(QStringLiteral("Stereo3DDepth"))
                                                                                        : image.text(QStringLiteral("Stereo3DDepth"));
        // the views drawn from the picture the user chose (the shown picture, or the original before the camera's
        // effects), the 2D picture staying the shown one
        const QImage source = depthPicture.isNull() ? image : depthPicture.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        result = second.isNull() ? Picture() : Picture::fromDepth(source, second, depthMark, depthOptions);
        result.layout = result.isStereo() ? packing : Layout::None;
    } else if (packing == Layout::MultiView) {
        result.left = image;
        result.right = second;
        result.layout = packing;
        if (order == EyeOrder::RightFirst)
            result = result.swapped();
    } else if (source == Source::FileMark && (format == "png" || format == "pns") && packing == Layout::SideBySide) {
        result = unpackPng(image, order);
    } else {
        result = unpackWithDepth(image, packing, order, depthOptions);
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
}

namespace
{
Picture readerPicture(QImageReader &reader, const QImage &primary, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Picture::Options &options, OutputMode output, bool overMark);
}

Picture Picture::fromReader(QImageReader &reader, const QImage &primary, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Options &options, OutputMode output)
{
    return readerPicture(reader, primary, metadata, format, header, options, output, false);
}

Picture Picture::fromReader(const QString &path, QImageReader &reader, const QImage &primary, const QString &metadata, const QByteArray &format,
    const QByteArray &header, const Options &fileOptions, OutputMode output)
{
    bool overMark = false;
    const Options options = withFileChoice(path, fileOptions, &overMark);
    return readerPicture(reader, primary, metadata, format, header, options, output, overMark);
}

Picture Picture::fromImages(const QString &path, const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format,
    const QByteArray &header, const Options &fileOptions, OutputMode output)
{
    bool overMark = false;
    const Options options = withFileChoice(path, fileOptions, &overMark);
    return fromImagesComparing(image, second, metadata, format, header, options, output, {}, {}, overMark);
}

namespace
{
using View = Picture::View;
Picture readerPicture(QImageReader &reader, const QImage &primary, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Picture::Options &options, OutputMode output, bool overMark)
{
    QImage first = primary;
    QImage second;
    const bool indexed = metadata.startsWith(QLatin1String("multiView")) && !reader.text(QStringLiteral("Stereo3DViewIndices")).isEmpty()
        && options.view != View::Mono;
    if (indexed) {
        const auto indices = stereoViewIndices(reader);
        if (!indices || !reader.jumpToImage(indices->first)) {
            Picture result;
            result.original = primary;
            return result;
        }
        first = reader.read();
        if (reader.jumpToImage(indices->second)) {
            second = reader.read();
        }
    } else if (hasSecondImage(reader.imageCount(), format, metadata) && reader.jumpToImage(1)) {
        second = reader.read();
    }
    Picture result = fromImagesComparing(first, second, metadata, format, header, options, output, {}, {}, overMark);
    if (indexed && options.view == View::Stereo) {
        result.original = primary;
    }
    return result;
}
}

Picture Picture::fromImages(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Options &options, OutputMode output)
{
    return fromImagesComparing(image, second, metadata, format, header, options, output);
}

Picture Picture::fromImages(const QImage &image, const QImage &second, const QString &metadata, const QByteArray &format, const QByteArray &header,
    const Options &options, const DepthOptions &depthOptions, OutputMode output)
{
    return fromImagesComparing(image, second, metadata, format, header, options, output, depthOptions);
}

namespace
{
// One view of an interleaved, checkerboard or quincunx-sampled picture at the size of the whole frame, as a display
// upconverts it (H.264 and H.265 Table D-8; H.264 V15, 08/2024 numbers it D-8, V16, 06/2026 D-9): the samples of the view stay
// where they are, and each missing one is the mean of its two neighbours across the interleaving. phase is the first row,
// column or checker of the view. A quincunx half holds the samples of a full-width view taken on even columns of even rows and odd columns of odd
// rows, both views alike (FFmpeg's AV_STEREO3D_SIDEBYSIDE_QUINCUNX).
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

namespace
{
Picture unpackWithDepth(const QImage &image, Picture::Layout layout, Picture::EyeOrder order, const Picture::DepthOptions &depthOptions)
{
    using Layout = Picture::Layout;
    using EyeOrder = Picture::EyeOrder;
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
    } else if (layout == Layout::SideBySide || layout == Layout::TopAndBottom) {
        const QString separationText = image.text(QStringLiteral("Stereo3DJpsSeparation"));
        bool ok = true;
        const int separation = separationText.isEmpty() ? 0 : separationText.toInt(&ok);
        const bool side = layout == Layout::SideBySide;
        const int size = (side ? image.width() : image.height()) - separation;
        if (!ok || separation < 0 || separation > 255 || size < 2 || size % 2) {
            result.layout = Layout::None;
            result.original = image;
            return result;
        }
        if (side) {
            const int width = size / 2;
            result.left = image.copy(0, 0, width, image.height());
            result.right = image.copy(width + separation, 0, width, image.height());
        } else {
            const int height = size / 2;
            result.left = image.copy(0, 0, image.width(), height);
            result.right = image.copy(0, height + separation, image.width(), height);
        }
    } else if (layout >= Layout::DepthSideBySide && layout <= Layout::DepthBackground) {
        // a picture and its depth in one frame: the views are drawn, so the order is where the picture is
        const QString depthMark = image.text(QStringLiteral("Stereo3DDepth"));
        const int w = image.width(), h = image.height();
        Picture drawn;
        if (layout == Layout::DepthSideBySide && w % 2 == 0 && w >= 2) {
            const QImage first = image.copy(0, 0, w / 2, h), second = image.copy(w / 2, 0, w / 2, h);
            drawn = order == EyeOrder::LeftFirst ? Picture::fromDepth(first, second, depthMark, depthOptions)
                                                 : Picture::fromDepth(second, first, depthMark, depthOptions);
        } else if (layout == Layout::DepthTopAndBottom && h % 2 == 0 && h >= 2) {
            const QImage first = image.copy(0, 0, w, h / 2), second = image.copy(0, h / 2, w, h / 2);
            drawn = order == EyeOrder::LeftFirst ? Picture::fromDepth(first, second, depthMark, depthOptions)
                                                 : Picture::fromDepth(second, first, depthMark, depthOptions);
        } else if (layout == Layout::DepthGraphics && h % 4 == 0 && h >= 4) {
            const int q = h / 4;
            drawn = Picture::fromDepth(image.copy(0, 0, w, q), image.copy(0, q, w, q), depthMark, depthOptions, image.copy(0, 2 * q, w, q),
                image.copy(0, 3 * q, w, q), false);
        } else if (layout == Layout::DepthBackground && w % 2 == 0 && h % 2 == 0 && w >= 2 && h >= 2) {
            const int hw = w / 2, hh = h / 2;
            drawn = Picture::fromDepth(image.copy(0, 0, hw, hh), image.copy(hw, 0, hw, hh), depthMark, depthOptions, image.copy(0, hh, hw, hh),
                image.copy(hw, hh, hw, hh), true);
        }
        if (drawn.isStereo()) {
            result.left = drawn.left;
            result.right = drawn.right;
        } else {
            result.layout = Layout::None;
        }
        result.original = image;
        return result;
    } else {
        result.layout = Layout::None;
    }
    result.original = image;
    return order == EyeOrder::RightFirst ? result.swapped() : result;
}

}

Picture Picture::fromPackedImage(const QImage &image, Layout layout, EyeOrder order) { return unpackWithDepth(image, layout, order, {}); }

namespace
{
// An image as planes for libstereo-dibr: RGBA, 8 or 16 bits a sample.
struct Planes {
    int bytes = 1;
    std::vector<uint8_t> data[4];
    stereo_dibr_picture picture {};
};

void toPlanes(const QImage &image, Planes &planes, bool wide)
{
    const QImage source = image.convertToFormat(wide ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    const int w = source.width(), h = source.height();
    planes.bytes = wide ? 2 : 1;
    planes.picture.width = w;
    planes.picture.height = h;
    planes.picture.nb_planes = 4;
    for (int k = 0; k < 4; k++) {
        planes.data[k].assign(size_t(w) * h * planes.bytes, 0);
        planes.picture.plane[k] = { planes.data[k].data(), ptrdiff_t(w) * planes.bytes, planes.bytes, 0, 0, k == 3 ? (wide ? 65535u : 255u) : 0u };
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int k = 0; k < 4; k++) {
                if (wide)
                    reinterpret_cast<uint16_t *>(planes.data[k].data())[size_t(y) * w + x]
                        = reinterpret_cast<const uint16_t *>(source.constScanLine(y))[4 * x + k];
                else
                    planes.data[k][size_t(y) * w + x] = source.constScanLine(y)[4 * x + k];
            }
        }
    }
}

QImage fromPlanes(const Planes &planes)
{
    const int w = planes.picture.width, h = planes.picture.height;
    const bool wide = planes.bytes == 2;
    QImage image(w, h, wide ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int k = 0; k < 4; k++) {
                if (wide)
                    reinterpret_cast<uint16_t *>(image.scanLine(y))[4 * x + k]
                        = reinterpret_cast<const uint16_t *>(planes.data[k].data())[size_t(y) * w + x];
                else
                    image.scanLine(y)[4 * x + k] = planes.data[k][size_t(y) * w + x];
            }
        }
    }
    return image;
}

// A depth map as one plane of samples: 16 bits when the image has more than 8 a channel.
QImage depthPlane(const QImage &depth)
{
    const bool wide = depth.format() == QImage::Format_Grayscale16 || depth.depth() >= 48;
    return depth.convertToFormat(wide ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8);
}
}

Picture Picture::fromDepth(const QImage &picture, const QImage &depth, const QString &depthMark, const DepthOptions &options, const QImage &layer,
    const QImage &layerDepth, bool layerIsBackground)
{
    Picture result;
    result.original = picture;
    if (picture.isNull() || depth.isNull() || (!layer.isNull() && (layer.size() != picture.size() || layerDepth.isNull()))) {
        return result;
    }
    const QImage plane = depthPlane(depth);
    const int bits = plane.format() == QImage::Format_Grayscale16 ? 16 : 8;

    // what the samples mean: the mark, over the options' range, screen and shift
    stereo_depth_convention convention {};
    convention.kind = STEREO_DEPTH_NEARNESS;
    convention.bits = bits;
    convention.zero = options.zero;
    convention.range = options.range;
    convention.shift = options.shift;
    uint16_t model[65];
    int nbModel = 0;
    if (!depthMark.isEmpty() && stereo_depth_read_mark(depthMark.toUtf8().constData(), &convention, model, &nbModel) < 0) {
        return result;
    }
    convention.bits = bits;
    std::vector<uint16_t> lut;
    if (convention.kind == STEREO_DEPTH_NONLINEAR_DISPARITY && nbModel >= 3) {
        lut.resize(size_t(1) << bits);
        if (stereo_depth_nonlinear_lut(model, nbModel, bits, lut.data()) == 0)
            convention.lut = lut.data();
    }
    std::vector<float> table(size_t(1) << bits);
    if (stereo_depth_parallax_table(&convention, picture.width(), table.data()) < 0) {
        return result;
    }

    const bool wide = picture.depth() > 32;
    Planes in, left, right, second;
    toPlanes(picture, in, wide);
    toPlanes(picture, left, wide);
    toPlanes(picture, right, wide);
    const stereo_dibr_depth depthMap { plane.constBits(), plane.bytesPerLine(), bits / 8, plane.width(), plane.height() };
    stereo_dibr_params params;
    stereo_dibr_params_default(&params);
    params.view = options.view == DepthOptions::View::Center ? STEREO_DIBR_VIEW_CENTER : STEREO_DIBR_VIEW_LEFT;
    params.warp = int(options.warp);
    params.fill = int(options.fill);
    params.smooth = options.smooth ? STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN : STEREO_DIBR_SMOOTH_OFF;
    params.sigma_h = options.sigmaH;
    params.sigma_v = options.sigmaV;
    params.radius = options.radius;

    stereo_dibr *dibr = stereo_dibr_alloc();
    int ret = -2;
    if (dibr && layer.isNull()) {
        ret = stereo_dibr_render(dibr, &params, table.data(), int(table.size()), &in.picture, &depthMap, &left.picture, &right.picture);
    } else if (dibr) {
        toPlanes(layer, second, wide);
        const QImage layerPlane = depthPlane(layerDepth).convertToFormat(plane.format());
        const stereo_dibr_depth layerMap { layerPlane.constBits(), layerPlane.bytesPerLine(), bits / 8, layerPlane.width(), layerPlane.height() };
        // a layer's samples of 0 hold nothing: no background there, no graphics there
        std::vector<float> layerTable = table;
        layerTable[0] = NAN;
        ret = stereo_dibr_render_layer(dibr, &params, table.data(), int(table.size()), &in.picture, &depthMap,
            layerIsBackground ? STEREO_DIBR_LAYER_BACKGROUND : STEREO_DIBR_LAYER_OVERLAY, &second.picture, layerTable.data(), int(layerTable.size()),
            &layerMap, &left.picture, &right.picture);
    }
    stereo_dibr_free(dibr);
    if (ret != 0) {
        return result;
    }
    result.left = fromPlanes(left);
    result.right = fromPlanes(right);
    result.layout = Layout::DepthSeparate;
    return result;
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
