// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QPointF>
namespace Stereo
{
STEREO_LAYER_EXPORT QImage renderEyes(const Picture& picture, const QSize& eyePixels, qreal zoom = 1, const QPointF& pan = {});
STEREO_LAYER_EXPORT QImage renderMono(const QImage& image, const QSize& pixels, qreal zoom = 1, const QPointF& pan = {});
}
