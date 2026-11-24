// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "iconsurface.h"
#include "surface_p.h"

namespace Stereo
{
IconSurface::IconSurface()
    : m_surface(std::make_unique<Surface>())
{
}

IconSurface::~IconSurface() = default;

bool IconSurface::show(QWindow *window, const QRect &rect, const Picture &picture)
{
    const bool deep = picture.left.depth() == 64 || picture.right.depth() == 64;
    const bool alpha = picture.left.hasAlphaChannel() || picture.right.hasAlphaChannel();
    const auto format = alpha ? QImage::Format_RGBA64_Premultiplied : QImage::Format_RGBX64;
    const QImage pair = deep ? picture.sideBySide(format) : picture.sideBySide();
    return m_surface->show(window, rect, pair, true, true);
}

void IconSurface::clear()
{
    m_surface->reset();
}

QString IconSurface::failure() const
{
    return m_surface->failure();
}
}
