// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "mark_p.h"
#include "stereo-policy.h"

namespace Stereo
{
Mark readMark(const QString &value)
{
    int layout = 0;
    int order = 0;
    int anaglyph = 0;
    Mark mark;
    mark.known = stereo_policy_read_mark(value.toUtf8().constData(), &layout, &order, &anaglyph) == 1;
    mark.layout = Picture::Layout(layout);
    mark.order = Picture::EyeOrder(order);
    mark.anaglyph = Picture::AnaglyphColorPair(anaglyph);
    return mark;
}
}
