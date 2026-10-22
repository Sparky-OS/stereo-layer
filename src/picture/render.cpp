// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "render.h"
#include <QPainter>
#include <algorithm>
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
// The samples of the view at phase of an interleaved, checkerboard or quincunx-sampled frame, each row or column
// as the frame carries it (Table D-8 types 0 to 2, and type 3 with quincunx sampling: H.264 V15, 08/2024): even
// rows, columns or checkers for phase 0. A quincunx half is a view's samples already side by side.
QImage storedView(const QImage &frame, Picture::Layout layout, int phase)
{
    const QImage source = frame.convertToFormat(QImage::Format_RGBA8888);
    const int w = source.width(), h = source.height();
    QImage view;
    if (layout == Picture::Layout::Rows) {
        view = QImage(w, (h - phase + 1) / 2, QImage::Format_RGBA8888);
        for (int y = 0; y < view.height(); ++y)
            std::copy_n(source.constScanLine(2 * y + phase), 4 * w, view.scanLine(y));
    } else if (layout == Picture::Layout::Columns || layout == Picture::Layout::Checkerboard) {
        view = QImage(w / 2, h, QImage::Format_RGBA8888);
        for (int y = 0; y < h; ++y) {
            // a checkerboard view's samples shift by one column from row to row
            const int first = layout == Picture::Layout::Checkerboard ? (phase + y) % 2 : phase;
            const uchar *in = source.constScanLine(y);
            uchar *out = view.scanLine(y);
            for (int x = 0; x < view.width(); ++x)
                std::copy_n(in + 4 * std::min(2 * x + first, w - 1), 4, out + 4 * x);
        }
    } else if (layout == Picture::Layout::SideBySideQuincunx) {
        view = source.copy(phase * (w / 2), 0, w / 2, h);
    }
    return view;
}
// Whether view keeps, at their places, the samples storedView() takes for phase: the views of Picture::fromPackedImage
// keep their own samples where they are and fill in the others
bool keepsPhase(const QImage &view, const QImage &frame, Picture::Layout layout, int phase)
{
    const QImage v = view.convertToFormat(QImage::Format_RGBA8888);
    const QImage f = frame.convertToFormat(QImage::Format_RGBA8888);
    const bool quincunx = layout == Picture::Layout::SideBySideQuincunx;
    if (v.height() != f.height() || v.width() != (quincunx ? f.width() / 2 * 2 : f.width()))
        return false;
    for (int y = 0; y < v.height(); ++y) {
        const quint32 *a = reinterpret_cast<const quint32 *>(v.constScanLine(y));
        const quint32 *b = reinterpret_cast<const quint32 *>(f.constScanLine(y));
        for (int x = 0; x < v.width(); ++x) {
            bool own;
            int from = x;
            if (layout == Picture::Layout::Rows) {
                own = y % 2 == phase;
            } else if (layout == Picture::Layout::Columns) {
                own = x % 2 == phase;
            } else if (layout == Picture::Layout::Checkerboard) {
                own = (x + y) % 2 == phase;
            } else {
                own = (x + y) % 2 == 0;
                from = phase * (f.width() / 2) + x / 2;
            }
            if (own && a[x] != b[from])
                return false;
        }
    }
    return true;
}
// The samples a stereo surface hands over for each eye: the eyes as they are, or, for the packings whose views
// Picture fills in to the shape of the frame, the view's own samples from the picture's frame
void storedEyes(const Picture &picture, QImage *left, QImage *right)
{
    *left = picture.left;
    *right = picture.right;
    const auto layout = picture.layout;
    if (picture.original.isNull()
        || (layout != Picture::Layout::Rows && layout != Picture::Layout::Columns && layout != Picture::Layout::Checkerboard
            && layout != Picture::Layout::SideBySideQuincunx))
        return;
    int phases[2] = {-1, -1};
    for (int eye = 0; eye < 2; ++eye) {
        for (int phase = 0; phase < 2 && phases[eye] < 0; ++phase) {
            if (keepsPhase(eye ? picture.right : picture.left, picture.original, layout, phase))
                phases[eye] = phase;
        }
    }
    if (phases[0] < 0 || phases[1] < 0)
        return;
    *left = storedView(picture.original, layout, phases[0]);
    *right = storedView(picture.original, layout, phases[1]);
    if (left->size() != right->size()) {
        *left = picture.left;
        *right = picture.right;
    }
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
EyeCrop cropEyes(const Picture &picture, const QSize &eyePixels, qreal zoom, const QPointF &pan)
{
    if (!picture.isStereo() || !valid(eyePixels, zoom, pan) || picture.left.size() != picture.right.size())
        return {};
    QImage left, right;
    storedEyes(picture, &left, &right);
    // where a view shows, with the shape of a view
    const QRectF dest = destination(picture.left.size(), eyePixels, zoom, pan);
    const QRectF visible = dest.intersected(QRectF(QPointF(), QSizeF(eyePixels)));
    if (visible.isEmpty() || dest.isEmpty())
        return {};
    // the whole samples that cover what shows, copied as they are
    const qreal sx = left.width() / dest.width();
    const qreal sy = left.height() / dest.height();
    const int x0 = std::max(0, int(std::floor((visible.left() - dest.left()) * sx + 1e-6)));
    const int y0 = std::max(0, int(std::floor((visible.top() - dest.top()) * sy + 1e-6)));
    const int x1 = std::min(left.width(), int(std::ceil((visible.right() - dest.left()) * sx - 1e-6)));
    const int y1 = std::min(left.height(), int(std::ceil((visible.bottom() - dest.top()) * sy - 1e-6)));
    if (x1 <= x0 || y1 <= y0)
        return {};
    const QRect source(x0, y0, x1 - x0, y1 - y0);
    const bool alpha = left.hasAlphaChannel() || right.hasAlphaChannel();
    EyeCrop result;
    result.packed = QImage(source.width() * 2, source.height(), alpha ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    QPainter painter(&result.packed);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPoint(0, 0), left, source);
    painter.drawImage(QPoint(source.width(), 0), right, source);
    painter.end();
    result.shown = QRectF(dest.left() + x0 / sx, dest.top() + y0 / sy, source.width() / sx, source.height() / sy);
    return result;
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
