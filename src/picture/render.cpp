// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "render.h"
#include <QPainter>
#include <cmath>
namespace Stereo
{
namespace
{
QRectF destination(const QSize &image, const QSize &pixels, qreal zoom, const QPointF &pan)
{
    const QSize fitted = image.scaled(pixels, Qt::KeepAspectRatio);
    const QSizeF scaled(fitted.width() * zoom, fitted.height() * zoom);
    return QRectF(QPointF((pixels.width() - scaled.width()) / 2, (pixels.height() - scaled.height()) / 2) + pan, scaled);
}
bool valid(const QSize &pixels, qreal zoom, const QPointF &pan)
{
    return !pixels.isEmpty() && pixels.width() <= 8192 && pixels.height() <= 16384 && qint64(pixels.width()) * pixels.height() <= 64 * 1024 * 1024
        && std::isfinite(zoom) && zoom > 0 && std::isfinite(pan.x()) && std::isfinite(pan.y());
}
}
QImage renderEyes(const Picture &picture, const QSize &eyePixels, qreal zoom, const QPointF &pan)
{
    if (!picture.isStereo() || !valid(eyePixels, zoom, pan))
        return {};
    QImage image(eyePixels.width() * 2, eyePixels.height(), QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF rect = destination(picture.left.size(), eyePixels, zoom, pan);
    painter.setClipRect(QRect(QPoint(), eyePixels));
    painter.drawImage(rect, (zoom == 1 ? picture.left.scaled(rect.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : picture.left));
    painter.setClipRect(QRect(QPoint(eyePixels.width(), 0), eyePixels));
    painter.drawImage(rect.translated(eyePixels.width(), 0),
        (zoom == 1 ? picture.right.scaled(rect.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : picture.right));
    return image;
}
QImage renderMono(const QImage &source, const QSize &pixels, qreal zoom, const QPointF &pan)
{
    if (source.isNull() || !valid(pixels, zoom, pan))
        return {};
    QImage image(pixels, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF rect = destination(source.size(), pixels, zoom, pan);
    painter.drawImage(rect, (zoom == 1 ? source.scaled(rect.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : source));
    return image;
}
}
