// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "animatedpicture.h"
#include "jxlmark_p.h"
#include "mark_p.h"

#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPainter>
#include <QXmlStreamReader>
#include <QtSvg/private/qsvgdocument_p.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Stereo
{
Q_DECLARE_LOGGING_CATEGORY(qLcPicture)

namespace
{
QString message(const char *text) { return QCoreApplication::translate("Stereo::AnimatedPicture", text); }

quint32 big32(const QByteArray &bytes, qsizetype at)
{
    return (quint32(quint8(bytes.at(at))) << 24) | (quint32(quint8(bytes.at(at + 1))) << 16) | (quint32(quint8(bytes.at(at + 2))) << 8)
        | quint32(quint8(bytes.at(at + 3)));
}

// An ISOBMFF file (an AVIF image sequence) whose track carries the stereo video scheme (ISO/IEC 14496-12:2026 8.15 and
// 13.5.4.2: a restricted sample entry with SchemeTypeBox 'stvi' and a StereoVideoBox in its scheme information).
bool hasStereoVideoScheme(QIODevice &device)
{
    const qint64 end = device.size();
    qint64 position = 0;
    while (position + 8 <= end && device.seek(position)) {
        const QByteArray head = device.read(8);
        if (head.size() < 8) {
            return false;
        }
        qint64 size = big32(head, 0);
        if (size == 1) {
            const QByteArray large = device.read(8);
            if (large.size() < 8) {
                return false;
            }
            size = qint64((quint64(big32(large, 0)) << 32) | big32(large, 4));
        } else if (size == 0) {
            size = end - position;
        }
        if (size < 8 || size > end - position) {
            return false;
        }
        if (head.sliced(4, 4) == "moov" && size <= 64 * 1024 * 1024 && device.seek(position + 8)) {
            const QByteArray movie = device.read(size - 8);
            for (qsizetype at = movie.indexOf("stvi"); at >= 0; at = movie.indexOf("stvi", at + 1)) {
                // a box of that type, or the scheme type of a 'schm' box
                if (at >= 4 && big32(movie, at - 4) >= 8 && at - 4 + qsizetype(big32(movie, at - 4)) <= movie.size()) {
                    return true;
                }
                if (at >= 8 && movie.sliced(at - 8, 4) == "schm") {
                    return true;
                }
            }
            return false;
        }
        position += size;
    }
    return false;
}

// How often a drawing's animations play again after the first time: -1 when one of them repeats for ever.
int drawingLoops(const QByteArray &document)
{
    QXmlStreamReader xml(document);
    int loops = 0;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView count = xml.attributes().value(QLatin1String("repeatCount"));
        if (count == QLatin1String("indefinite") || xml.attributes().value(QLatin1String("repeatDur")) == QLatin1String("indefinite")) {
            return -1;
        }
        bool ok = false;
        const double times = count.toDouble(&ok);
        if (ok && times > 1) {
            loops = std::max(loops, int(std::ceil(times)) - 1);
        }
    }
    return loops;
}
}

class AnimatedPicture::Private
{
public:
    QString reason;
    bool null = true;
    // the decision of the first frame, kept for every frame
    bool stereo = false;
    Picture::Layout layout = Picture::Layout::None;
    Picture::EyeOrder order = Picture::EyeOrder::LeftFirst;
    Picture::AnaglyphColorPair anaglyph = Picture::AnaglyphColorPair::None;
    Picture::Source source = Picture::Source::None;
    bool swap = false;
    // a raster: its frames, decoded once, and where decoding stopped
    QList<QImage> frames;
    QList<int> delays;
    int declared = 0;
    int loops = 0;
    QString failure;
    // a drawing
    std::unique_ptr<QSvgDocument> document;
    QSize documentSize;
    qint64 documentDuration = 0;

    void decide(const QString &path, const QImage &first, const QString &metadata, const QByteArray &format, const QByteArray &header,
        const Picture::Options &options)
    {
        const Picture decided = Picture::fromImages(path, first, QImage(), metadata, format, header, options, OutputMode::Unknown);
        stereo = decided.isStereo() && decided.layout != Picture::Layout::MultiView && decided.layout != Picture::Layout::FrameSequential
            && decided.layout != Picture::Layout::BlockLaced && decided.layout != Picture::Layout::DepthSeparate;
        if (!stereo) {
            return;
        }
        layout = decided.layout;
        order = decided.eyeOrder;
        anaglyph = decided.anaglyph;
        source = decided.source;
        const Picture plain = take(first);
        swap = plain.left != decided.left && plain.left == decided.right;
    }

    Picture take(const QImage &image) const
    {
        return anaglyph != Picture::AnaglyphColorPair::None ? Picture::fromAnaglyph(image, anaglyph) : Picture::fromPackedImage(image, layout, order);
    }

    Picture split(const QImage &image) const
    {
        if (!stereo) {
            Picture flat;
            flat.original = image;
            return flat;
        }
        Picture pair = take(image);
        if (swap) {
            pair = pair.swapped();
        }
        pair.source = source;
        pair.original = image;
        return pair;
    }

    // A frame that cannot be decoded ends what can be shown: no time after it is given a picture.
    qint64 playable() const { return failure.isEmpty() ? -1 : std::accumulate(delays.cbegin(), delays.cend(), qint64(0)); }

    void openDrawing(const QString &path, QFile &file, const QByteArray &header, const QString &layout, const Picture::Options &options)
    {
        QByteArray contents = file.readAll();
        if (contents.startsWith("\x1f\x8b")) {
            contents = gunzipHeader(contents, 64 * 1024 * 1024);
        }
        document = QSvgDocument::load(contents, {}, QtSvg::AnimatorType::Controlled);
        if (!document) {
            reason = message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This drawing cannot be read as SVG."));
            return;
        }
        documentSize = document->size();
        documentDuration = document->animationDuration();
        loops = drawingLoops(contents);
        null = documentSize.isEmpty();
        if (null) {
            reason = message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This drawing has no size to be drawn at."));
            return;
        }
        decide(path, draw(0, documentSize), layout, "svg", header, options);
    }

    QImage draw(qint64 msecs, const QSize &size) const
    {
        document->animator()->setAnimatorTime(msecs);
        document->animator()->advanceAnimations();
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        document->draw(&painter, QRectF(QPointF(), QSizeF(size)));
        return image;
    }

    QSize drawingSize(const QSize &eyeSize) const
    {
        if (eyeSize.isEmpty()) {
            return documentSize;
        }
        if (!stereo) {
            return eyeSize;
        }
        switch (layout) {
        case Picture::Layout::TopAndBottom:
        case Picture::Layout::DepthTopAndBottom:
            return QSize(eyeSize.width(), 2 * eyeSize.height());
        default:
            return QSize(2 * eyeSize.width(), eyeSize.height());
        }
    }
};

AnimatedPicture::AnimatedPicture(const QString &path, const Picture::Options &options)
    : d(std::make_unique<Private>())
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        d->reason = message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This picture cannot be opened: %1.")).arg(file.errorString());
        return;
    }
    const QByteArray header = file.peek(1024 * 1024);
    const QByteArray start = header.startsWith("\x1f\x8b") ? gunzipHeader(header, 1024 * 1024) : header;
    if (isSvgDocument(start)) {
        d->openDrawing(path, file, header, svgStereoLayout(start), options);
        return;
    }
    if (hasStereoVideoScheme(file)) {
        d->reason = message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This animated picture keeps its two views as a stereo video track (ISO/IEC 14496-12, stereo video scheme), "
                            "and its reader gives each frame only one view wide, so it cannot be shown as a pair."));
        return;
    }
    file.seek(0);
    QImageReader reader(&file);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format();
    if (format.isEmpty()) {
        d->reason = isJxlFile(header) ? message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This JPEG XL picture cannot be shown: no JPEG XL image reader is installed (kimageformat6-plugins)."))
                                      : message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This picture cannot be shown: no image reader for its format is installed (%1).")).arg(reader.errorString());
        return;
    }
    d->declared = reader.imageCount();
    d->loops = reader.loopCount();
    QString metadata = reader.text(QStringLiteral("Stereo3DLayout"));
    for (;;) {
        if (d->declared > 0 && d->frames.size() >= d->declared) {
            break;
        }
        const QImage image = reader.read();
        if (image.isNull()) {
            // The end of a file that declared no count is not a failure; anything else is.
            if (d->declared > 0 || d->frames.isEmpty()) {
                d->failure = message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "Frame %1 of this animated picture cannot be decoded: %2.")).arg(d->frames.size() + 1).arg(reader.errorString());
            }
            break;
        }
        d->frames.append(image);
        d->delays.append(std::max(0, reader.nextImageDelay()));
        if (d->declared <= 0 && !reader.canRead()) {
            break;
        }
    }
    if (d->frames.isEmpty()) {
        d->reason = d->failure;
        return;
    }
    d->reason = d->failure;
    d->null = false;
    const QImage &first = d->frames.first();
    if (metadata.isEmpty()) {
        metadata = first.text(QStringLiteral("Stereo3DLayout"));
    }
    if (metadata.isEmpty() && format == "jxl") {
        QString why;
        metadata = jxlStereoLayout(readJxlMark(file), first.size(), &why);
        if (!why.isEmpty()) {
            qCDebug(qLcPicture).noquote() << path << "is shown as authored:" << why;
        }
    }
    d->decide(path, first, metadata, format, header, options);
}

AnimatedPicture::~AnimatedPicture() = default;

bool AnimatedPicture::isNull() const { return d->null; }

bool AnimatedPicture::isStereo() const { return !d->null && d->stereo; }

QString AnimatedPicture::reason() const { return d->reason; }

int AnimatedPicture::frameCount() const
{
    return d->document ? 0 : std::max<int>(d->declared, int(d->frames.size()));
}

QList<int> AnimatedPicture::delays() const { return d->delays; }

int AnimatedPicture::loopCount() const { return d->loops; }

qint64 AnimatedPicture::duration() const
{
    return d->document ? d->documentDuration : std::accumulate(d->delays.cbegin(), d->delays.cend(), qint64(0));
}

int AnimatedPicture::frameAt(qint64 msecs) const
{
    if (d->document || d->frames.isEmpty()) {
        return 0;
    }
    const qint64 total = duration();
    const qint64 playable = d->playable();
    if (playable >= 0 && msecs >= playable) {
        return int(d->frames.size());
    }
    if (total <= 0) {
        return 0;
    }
    msecs = std::max<qint64>(0, msecs);
    if (d->loops >= 0 && msecs >= total * (d->loops + 1)) {
        return int(d->frames.size()) - 1;
    }
    qint64 time = msecs % total;
    for (int index = 0; index < d->frames.size(); ++index) {
        if (time < d->delays.at(index)) {
            return index;
        }
        time -= d->delays.at(index);
    }
    return int(d->frames.size()) - 1;
}

Picture AnimatedPicture::pairAt(qint64 msecs, const QSize &eyeSize)
{
    if (d->null) {
        return {};
    }
    if (d->document) {
        return d->split(d->draw(std::max<qint64>(0, msecs), d->drawingSize(eyeSize)));
    }
    return frame(frameAt(msecs));
}

Picture AnimatedPicture::frame(int index)
{
    if (d->null || d->document) {
        return {};
    }
    if (index < 0 || index >= d->frames.size()) {
        d->reason = d->failure.isEmpty() ? message(QT_TRANSLATE_NOOP("Stereo::AnimatedPicture", "This animated picture has no frame %1.")).arg(index + 1) : d->failure;
        return {};
    }
    return d->split(d->frames.at(index));
}
}
