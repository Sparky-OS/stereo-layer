// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QPointF>
#include <QRectF>
namespace Stereo
{
// Both eyes drawn at eyePixels each, fitted, zoomed and panned: a picture at a size, for a program that shows it
// itself. A stereo surface takes cropEyes() instead, which leaves the one resize to the compositor.
STEREO_LAYER_EXPORT QImage renderEyes(const Picture &picture, const QSize &eyePixels, qreal zoom = 1, const QPointF &pan = {});
// What a stereo surface hands the compositor for a picture shown in an area of eyePixels at zoom and pan: the part of
// each eye that shows, copied side by side at the resolution the eye has, left eye first, never scaled, and the
// rectangle of the area (in its pixels) the compositor resizes it to, once. A half packing stays half: the rows of a
// row-interleaved picture are its eyes' rows, its columns their columns (Picture::original's samples); the rectangle
// gives them the shape of a view (Picture::left's size). Null when the picture is not a pair, its views differ in
// size, or nothing of it shows.
struct STEREO_LAYER_EXPORT EyeCrop {
    QImage packed;
    QRectF shown;
};
STEREO_LAYER_EXPORT EyeCrop cropEyes(const Picture &picture, const QSize &eyePixels, qreal zoom = 1, const QPointF &pan = {});
STEREO_LAYER_EXPORT QImage renderMono(const QImage &image, const QSize &pixels, qreal zoom = 1, const QPointF &pan = {});
}
