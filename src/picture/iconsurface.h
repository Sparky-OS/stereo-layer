// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once

#include "picture.h"

#include <QRect>
#include <memory>

class QWindow;

namespace Stereo
{
class Surface;

// One inline icon painted above a Wayland window with the icon's original transparency.
// The caller places it in window coordinates and selects the single view on a 2D output.
class STEREO_LAYER_EXPORT IconSurface
{
public:
    IconSurface();
    ~IconSurface();

    bool show(QWindow *window, const QRect &rect, const Picture &picture);
    bool showView(QWindow *window, const QRect &rect, const QImage &view);
    void clear();
    QString failure() const;

private:
    std::unique_ptr<Surface> m_surface;
};
}
