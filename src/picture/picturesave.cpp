// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include "stereo-jpeg.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QTemporaryFile>
#include <QtEndian>
#include <cstdio>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace Stereo
{
namespace
{
using Layout = Picture::Layout;
using Order = Picture::EyeOrder;
QString message(const char *text) { return QCoreApplication::translate("Stereo::PictureSave", text); }
struct Segment {
    qsizetype begin, payload, end, prefix;
    unsigned marker;
};
struct Jpeg {
    QList<Segment> segments;
    qsizetype end = 0;
    int width = 0, height = 0;
    bool lossless = false;
};
// ITU-T T.81 (1992), Annex B: length excludes marker, FF00 is stuffed data;
// restart markers are in scans; other markers end a scan. Never search opaque payloads.
bool scan(const QByteArray &data, qsizetype begin, Jpeg &jpeg)
{
    if (begin < 0 || begin > data.size() - 2 || data.mid(begin, 2) != QByteArray::fromHex("ffd8"))
        return false;
    qsizetype pos = begin + 2;
    bool entropy = false, frame = false, scans = false;
    while (pos < data.size()) {
        if (entropy) {
            if (uchar(data[pos++]) != 0xff)
                continue;
            const qsizetype markerStart = pos - 1;
            while (pos < data.size() && uchar(data[pos]) == 0xff)
                ++pos;
            if (pos == data.size())
                return false;
            unsigned marker = uchar(data[pos]);
            if (marker == 0 || (marker >= 0xd0 && marker <= 0xd7)) {
                ++pos;
                continue;
            }
            pos = markerStart;
            entropy = false;
        }
        const qsizetype start = pos;
        if (uchar(data[pos++]) != 0xff)
            return false;
        while (pos < data.size() && uchar(data[pos]) == 0xff)
            ++pos;
        if (pos == data.size())
            return false;
        const qsizetype markerBegin = pos - 1;
        unsigned marker = uchar(data[pos++]);
        if (marker == 0xd9) {
            jpeg.end = pos;
            return frame && scans;
        }
        if (marker == 0 || marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7))
            return false;
        if (marker == 1)
            continue;
        if (pos > data.size() - 2)
            return false;
        unsigned length = qFromBigEndian<quint16>(data.constData() + pos);
        if (length < 2 || length > unsigned(data.size() - pos))
            return false;
        const qsizetype payload = pos + 2, end = pos + length;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (frame || length < 11)
                return false;
            unsigned components = uchar(data[payload + 5]);
            if (!components || length != 8 + components * 3)
                return false;
            jpeg.height = qFromBigEndian<quint16>(data.constData() + payload + 1);
            jpeg.width = qFromBigEndian<quint16>(data.constData() + payload + 3);
            if (!jpeg.width || !jpeg.height)
                return false;
            jpeg.lossless = marker == 0xc3 || marker == 0xc7 || marker == 0xcb || marker == 0xcf;
            frame = true;
        }
        if (marker == 0xda) {
            if (!frame || length < 8 || length != 6 + 2 * unsigned(uchar(data[payload])))
                return false;
            entropy = true;
            scans = true;
        }
        jpeg.segments.append({ markerBegin, payload, end, start, marker });
        pos = end;
    }
    return false;
}
QList<Segment> segments(const QByteArray &data, const Jpeg &jpeg, unsigned marker, const QByteArray &id)
{
    QList<Segment> result;
    for (auto segment : jpeg.segments)
        if (segment.marker == marker && segment.end - segment.payload >= id.size() && data.mid(segment.payload, id.size()) == id)
            result << segment;
    return result;
}
Picture decoded(const QByteArray &data, const QByteArray &format, const Picture::Options &options)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, format);
    QString metadata = reader.text(QStringLiteral("Stereo3DLayout"));
    QImage primary = reader.read();
    if (metadata.isEmpty())
        metadata = primary.text(QStringLiteral("Stereo3DLayout"));
    if (options.inputLayout != Layout::None || options.anaglyph != Picture::AnaglyphColorPair::None || options.view == Picture::View::Mono)
        metadata.clear(); // Explicit save corrections override even a mistaken declaration.
    return Picture::fromReader(reader, primary, metadata, format, data.left(1024 * 1024), options, OutputMode::Stereo);
}
bool equalPixels(const QImage &a, const QImage &b)
{
    return !a.isNull() && !b.isNull() && a.convertToFormat(QImage::Format_RGBA8888) == b.convertToFormat(QImage::Format_RGBA8888);
}
QString rewrite(const QByteArray &original, int layout, Order order, QByteArray &candidate)
{
    candidate.resize(original.size() + 65537);
    size_t size = 0;
    const int result = stereo_jpeg_correct(
        original.constData(), size_t(original.size()), layout, order == Order::RightFirst, candidate.data(), size_t(candidate.size()), &size);
    if (result == STEREO_JPEG_OK) {
        candidate.resize(qsizetype(size));
        return {};
    }
    candidate.clear();
    switch (result) {
    case STEREO_JPEG_LOSSLESS:
        return message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "Lossless JPEG cannot yet be verified by the picture reader; the file was kept."));
    case STEREO_JPEG_MPO:
        return message(QT_TRANSLATE_NOOP(
            "Stereo::PictureSave", "The MPO is not a valid complete two-view disparity pair with matching base-viewpoint references."));
    case STEREO_JPEG_DESCRIPTOR:
        return message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The JPS descriptor is invalid or ambiguous; correct its fields before saving."));
    case STEREO_JPEG_GEOMETRY:
        return message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The chosen JPS packing and separation do not fit the stored pixel dimensions."));
    case STEREO_JPEG_TRAILING:
        return message(
            QT_TRANSLATE_NOOP("Stereo::PictureSave", "The JPEG has unindexed trailing data; metadata-only save cannot preserve its offsets safely."));
    default:
        return message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The JPEG segments or scan data are truncated or invalid."));
    }
}
QString rewriteJps(const QByteArray &original, const Picture &chosen, QByteArray &candidate)
{
    int layout = 0;
    switch (chosen.layout) {
    case Layout::None:
        break;
    case Layout::SideBySide:
        layout = 2;
        break;
    case Layout::TopAndBottom:
        layout = 3;
        break;
    case Layout::Rows:
        layout = 1;
        break;
    default:
        return message(QT_TRANSLATE_NOOP(
            "Stereo::PictureSave", "JPS cannot describe the chosen packing. Choose side by side, top and bottom, rows, red/cyan or mono."));
    }
    if (chosen.anaglyph != Picture::AnaglyphColorPair::None) {
        if (chosen.anaglyph != Picture::AnaglyphColorPair::RedCyan)
            return message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "JPS can describe only red/cyan anaglyphs."));
        layout = 4;
    }
    return rewrite(original, layout, chosen.eyeOrder, candidate);
}
// Preserve filesystem metadata, including ACLs; the legacy swap xattr is consumed by this save.
// Failure to preserve an attribute aborts before replacement rather than discarding it.
bool copyAttributes(int source, int destination)
{
    ssize_t count = flistxattr(source, nullptr, 0);
    if (count < 0)
        return errno == ENOTSUP;
    QByteArray names(count, Qt::Uninitialized);
    if (flistxattr(source, names.data(), size_t(names.size())) != count)
        return false;
    for (qsizetype i = 0; i < names.size();) {
        QByteArray name = QByteArray(names.constData() + i);
        i += name.size() + 1;
        if (name == "user.stereo-policy.swap-eyes")
            continue;
        ssize_t size = fgetxattr(source, name.constData(), nullptr, 0);
        if (size < 0)
            return false;
        QByteArray bytes(size, Qt::Uninitialized);
        if (fgetxattr(source, name.constData(), bytes.data(), size_t(bytes.size())) != size
            || fsetxattr(destination, name.constData(), bytes.constData(), size_t(bytes.size()), 0))
            return false;
    }
    return true;
}
}
Picture::SaveResult Picture::saveCorrection(const QString &path, const Options &requested)
{
    QFile original(path);
    if (!original.open(QIODevice::ReadOnly))
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The original picture could not be read completely.")) };
    struct stat source {};
    if (fstat(original.handle(), &source) || !S_ISREG(source.st_mode) || QFileInfo(path).isSymLink())
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "Metadata-only save requires a regular file, not a symbolic link.")) };
    const QByteArray bytes = original.readAll();
    if (original.error() != QFileDevice::NoError)
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The original picture could not be read completely.")) };
    Jpeg jpeg;
    if (!scan(bytes, 0, jpeg))
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The JPEG segments or scan data are truncated or invalid.")) };
    if (jpeg.lossless)
        return { false,
            message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "Lossless JPEG cannot yet be verified by the picture reader; the file was kept.")) };
    const bool multi = !segments(bytes, jpeg, 0xe2, QByteArray("MPF\0", 4)).isEmpty();
    if (requested.inputOrder != Order::LeftFirst && requested.inputOrder != Order::RightFirst)
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The requested layout, eye order or view is invalid.")) };
    if (requested.view != View::Stereo && requested.view != View::LeftOnly && requested.view != View::RightOnly && requested.view != View::Mono)
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The requested layout, eye order or view is invalid.")) };
    if (multi
        && (requested.view == View::Mono || requested.anaglyph != AnaglyphColorPair::None
            || (requested.inputLayout != Layout::None && requested.inputLayout != Layout::MultiView)))
        return { false,
            message(QT_TRANSLATE_NOOP(
                "Stereo::PictureSave", "MPO stores separate disparity views; it cannot describe the chosen packing or a monoscopic correction.")) };
    if (!multi && requested.view != View::Mono && requested.inputLayout != Layout::None && requested.inputLayout != Layout::SideBySide
        && requested.inputLayout != Layout::TopAndBottom && requested.inputLayout != Layout::Rows)
        return { false,
            message(QT_TRANSLATE_NOOP(
                "Stereo::PictureSave", "JPS cannot describe the chosen packing. Choose side by side, top and bottom, rows, red/cyan or mono.")) };
    if (!multi && requested.view != View::Mono && requested.anaglyph != AnaglyphColorPair::None && requested.anaglyph != AnaglyphColorPair::RedCyan)
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "JPS can describe only red/cyan anaglyphs.")) };
    const QByteArray format = multi ? QByteArray("mpo") : QByteArray("jps");
    Options options = requested;
    if (options.view != View::Mono)
        options.view = View::Stereo;
    options.swapEyes = options.swapEyes != userSwapEyes(path);
    Picture chosen = decoded(bytes, format, options);
    if (chosen.original.isNull())
        return { false,
            message(QT_TRANSLATE_NOOP(
                "Stereo::PictureSave", "The picture could not be decoded with the chosen layout and eyes; the original was kept.")) };
    if (!multi && requested.inputLayout == Layout::None && requested.anaglyph == AnaglyphColorPair::None
        && QFileInfo(path).suffix().compare(QLatin1String("jps"), Qt::CaseInsensitive) != 0
        && segments(bytes, jpeg, 0xe3, QByteArray("_JPSJPS_", 8)).isEmpty())
        chosen = decoded(bytes, QByteArray("jpeg"), options);
    if (!chosen.isStereo() && options.swapEyes && options.view != View::Mono)
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The requested layout, eye order or view is invalid.")) };
    QByteArray candidate;
    QString error;
    if (multi) {
        if (chosen.layout != Layout::MultiView || chosen.anaglyph != AnaglyphColorPair::None)
            return { false,
                message(QT_TRANSLATE_NOOP("Stereo::PictureSave",
                    "MPO stores separate disparity views; it cannot describe the chosen packing or a monoscopic correction.")) };
        error = rewrite(bytes, 5, chosen.eyeOrder, candidate);
    } else
        error = rewriteJps(bytes, chosen, candidate);
    if (!error.isEmpty())
        return { false, error };
    Jpeg verified;
    if (!scan(candidate, 0, verified))
        return { false, message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The JPEG segments or scan data are truncated or invalid.")) };
    QTemporaryFile temporary(QFileInfo(path).absolutePath() + QStringLiteral("/.stereo-save-XXXXXX.") + QFileInfo(path).suffix());
    if (!temporary.open() || temporary.write(candidate) != candidate.size() || !temporary.flush() || !temporary.seek(0)
        || temporary.readAll() != candidate)
        return { false,
            message(QT_TRANSLATE_NOOP(
                "Stereo::PictureSave", "The temporary picture could not be written, read back or replaced; the original was kept.")) };
    const auto actual = Picture::load(temporary.fileName(), {}, OutputMode::Stereo);
    if ((chosen.isStereo()
            && (!actual.isStereo() || chosen.layout != actual.layout || chosen.anaglyph != actual.anaglyph || chosen.eyeOrder != actual.eyeOrder
                || !equalPixels(chosen.left, actual.left) || !equalPixels(chosen.right, actual.right)))
        || (!chosen.isStereo() && (actual.isStereo() || !equalPixels(chosen.original, actual.original))))
        return { false,
            message(QT_TRANSLATE_NOOP(
                "Stereo::PictureSave", "The picture could not be decoded with the chosen layout and eyes; the original was kept.")) };
    if (fchown(temporary.handle(), source.st_uid, source.st_gid) || fchmod(temporary.handle(), source.st_mode & 07777)
        || !copyAttributes(original.handle(), temporary.handle()) || fsync(temporary.handle()))
        return { false,
            message(
                QT_TRANSLATE_NOOP("Stereo::PictureSave", "The temporary picture's ownership, permissions or attributes could not be preserved.")) };
    struct stat current {};
    if (lstat(QFile::encodeName(path).constData(), &current) || current.st_dev != source.st_dev || current.st_ino != source.st_ino
        || current.st_mtim.tv_sec != source.st_mtim.tv_sec || current.st_mtim.tv_nsec != source.st_mtim.tv_nsec
        || current.st_ctim.tv_sec != source.st_ctim.tv_sec || current.st_ctim.tv_nsec != source.st_ctim.tv_nsec || !original.seek(0)
        || original.readAll() != bytes)
        return { false,
            message(QT_TRANSLATE_NOOP("Stereo::PictureSave", "The original picture changed during the save; retry with the current file.")) };
    if (::rename(QFile::encodeName(temporary.fileName()).constData(), QFile::encodeName(path).constData()))
        return { false,
            message(QT_TRANSLATE_NOOP(
                "Stereo::PictureSave", "The temporary picture could not be written, read back or replaced; the original was kept.")) };
    temporary.setAutoRemove(false);
    return { true, {} };
}
}
