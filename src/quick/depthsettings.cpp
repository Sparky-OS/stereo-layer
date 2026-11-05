// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "depthsettings.h"
#include <QCoreApplication>
#include <QHash>
#include <cstring>
#include <stereo-dibr.h>

namespace Stereo
{
struct DepthSettings::Private {
    stereo_dibr_options o;
};

DepthSettings::DepthSettings(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    stereo_dibr_options_default(&d->o);
}

DepthSettings::~DepthSettings()
{
    delete d;
}


// every setter holds the set within the library's limits and tells only a real change
#define SETTER(Setter, Field, Type, Member)                                                                                                          \
    void DepthSettings::Setter(Type value)                                                                                                           \
    {                                                                                                                                                \
        stereo_dibr_options next = d->o;                                                                                                             \
        next.Member = value;                                                                                                                         \
        stereo_dibr_options_clamp(&next);                                                                                                            \
        if (!memcmp(&next, &d->o, sizeof(next)))                                                                                                     \
            return;                                                                                                                                  \
        d->o = next;                                                                                                                                 \
        Q_EMIT changed();                                                                                                                            \
    }                                                                                                                                                \
    Type DepthSettings::Field() const                                                                                                                \
    {                                                                                                                                                \
        return d->o.Member;                                                                                                                          \
    }

SETTER(setView, view, int, view)
SETTER(setWarp, warp, int, warp)
SETTER(setFill, fill, int, fill)
SETTER(setSmooth, smooth, int, smooth)
SETTER(setSigmaH, sigmaH, double, sigma_h)
SETTER(setSigmaV, sigmaV, double, sigma_v)
SETTER(setRadius, radius, int, radius)
SETTER(setStrength, strength, double, strength)
SETTER(setZero, zero, double, zero)
SETTER(setShift, shift, double, shift)
SETTER(setNearDistance, nearDistance, double, z_near)
SETTER(setFarDistance, farDistance, double, z_far)

double DepthSettings::strengthMax()
{
    return STEREO_DIBR_STRENGTH_MAX;
}

double DepthSettings::shiftMax()
{
    return STEREO_DIBR_SHIFT_MAX;
}

int DepthSettings::radiusMax()
{
    return STEREO_DIBR_RADIUS_MAX;
}

QString DepthSettings::text() const
{
    if (stereo_dibr_options_is_default(&d->o))
        return QString();
    char buffer[512];
    return stereo_dibr_options_write(&d->o, buffer, sizeof(buffer)) < 0 ? QString() : QString::fromLatin1(buffer);
}

bool DepthSettings::setText(const QString &text)
{
    stereo_dibr_options next;
    if (text.isEmpty()) {
        stereo_dibr_options_default(&next);
    } else if (stereo_dibr_options_read(text.toLatin1().constData(), &next) != 1) {
        return false;
    }
    if (memcmp(&next, &d->o, sizeof(next))) {
        d->o = next;
        Q_EMIT changed();
    }
    return true;
}

bool DepthSettings::isDefault() const
{
    return stereo_dibr_options_is_default(&d->o);
}

void DepthSettings::reset()
{
    setText(QString());
}

Picture::DepthOptions DepthSettings::toOptions() const
{
    Picture::DepthOptions out;
    out.view = d->o.view == STEREO_DIBR_VIEW_CENTER ? Picture::DepthOptions::View::Center : Picture::DepthOptions::View::Left;
    out.warp = d->o.warp == STEREO_DIBR_WARP_BACKWARD ? Picture::DepthOptions::Warp::Backward
        : d->o.warp == STEREO_DIBR_WARP_LAYERED       ? Picture::DepthOptions::Warp::Layered
                                                      : Picture::DepthOptions::Warp::Forward;
    out.fill = d->o.fill == STEREO_DIBR_FILL_DEPTH_AIDED ? Picture::DepthOptions::Fill::DepthAided
        : d->o.fill == STEREO_DIBR_FILL_TELEA            ? Picture::DepthOptions::Fill::Telea
        : d->o.fill == STEREO_DIBR_FILL_NONE             ? Picture::DepthOptions::Fill::None
                                                         : Picture::DepthOptions::Fill::Background;
    out.smooth = d->o.smooth == STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN;
    out.sigmaH = d->o.sigma_h;
    out.sigmaV = d->o.sigma_v;
    out.radius = d->o.radius;
    out.range = d->o.strength;
    out.zero = d->o.zero;
    out.shift = d->o.shift;
    return out;
}

QString DepthSettings::label(const QString &key)
{
    static const char *context = "Stereo::DepthControls";
    // each text is spelled out where tr is read, so that the translation tools find it
    struct Entry {
        const char *key;
        QString text;
    };
    const Entry entries[] = {
        {"title", QCoreApplication::translate(context, "Picture and depth")},
        {"strength", QCoreApplication::translate(context, "Depth strength")},
        {"strengthTip",
         QCoreApplication::translate(context,
                                     "How deep the picture looks: the largest parallax in front of and behind the screen, in pixels of a picture 1920 wide. "
                                     "58 is about one degree of visual angle.")},
        {"zero", QCoreApplication::translate(context, "Screen plane")},
        {"zeroTip",
         QCoreApplication::translate(context,
                                     "Where the screen is in the depth of the picture, from 0 (the farthest) to 1 (the nearest). What is nearer comes out of the "
                                     "screen, what is farther stays behind it.")},
        {"shift", QCoreApplication::translate(context, "Shift")},
        {"picture", QCoreApplication::translate(context, "The picture is")},
        {"pictureTip",
         QCoreApplication::translate(context,
                                     "Which view the picture is. As the left eye it stays exactly as it is in that eye and the right eye is drawn; as the middle, "
                                     "both views are drawn.")},
        {"pictureLeft", QCoreApplication::translate(context, "the left eye")},
        {"pictureMiddle", QCoreApplication::translate(context, "the middle")},
        {"fill", QCoreApplication::translate(context, "Gap filling")},
        {"fillTip",
         QCoreApplication::translate(context, "How the places that the picture does not show, behind the objects in front, are filled.")},
        {"fillBackground", QCoreApplication::translate(context, "Background")},
        {"fillDepthAided", QCoreApplication::translate(context, "Depth-aided")},
        {"fillTelea", QCoreApplication::translate(context, "Inpainting")},
        {"fillNone", QCoreApplication::translate(context, "None")},
        {"warp", QCoreApplication::translate(context, "Warping")},
        {"warpForward", QCoreApplication::translate(context, "Forward")},
        {"warpBackward", QCoreApplication::translate(context, "Backward")},
        {"warpLayered", QCoreApplication::translate(context, "Layered")},
        {"smooth", QCoreApplication::translate(context, "Smooth the depth")},
        {"reach", QCoreApplication::translate(context, "Filling reach")},
        {"nearest", QCoreApplication::translate(context, "Nearest distance")},
        {"farthest", QCoreApplication::translate(context, "Farthest distance")},
        {"reset", QCoreApplication::translate(context, "Back to the default")},
    };
    for (const Entry &entry : entries)
        if (key == QLatin1String(entry.key))
            return entry.text;
    return QString();
}
}
