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
    // what the player's drawing chose for the video that plays
    bool hasChoice = false;
    int warp = 0, fill = 0, skippedWarp = -1, skippedFill = -1, fps = 0;
    bool keepsUp = true, onGpu = true, gpuLost = false, skippedMemory = false;
    // what the GPU has free, and the size and depth the video is drawn at
    double freeBytes = -1;
    int memWidth = 0, memHeight = 0, memBits = 8;
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
#define OWN_SETTER(Setter, Field, Member)                                                                                                            \
    void DepthSettings::Setter(int value)                                                                                                            \
    {                                                                                                                                                \
        stereo_dibr_options next = d->o;                                                                                                             \
        next.Member = value;                                                                                                                         \
        next.automatic = 0;                                                                                                                          \
        stereo_dibr_options_clamp(&next);                                                                                                            \
        if (!memcmp(&next, &d->o, sizeof(next)))                                                                                                     \
            return;                                                                                                                                  \
        d->o = next;                                                                                                                                 \
        Q_EMIT changed();                                                                                                                            \
    }                                                                                                                                                \
    int DepthSettings::Field() const                                                                                                                 \
    {                                                                                                                                                \
        return d->o.Member;                                                                                                                          \
    }

// the person's own warping or gap filling: the way is theirs from then on
OWN_SETTER(setWarp, warp, warp)
OWN_SETTER(setFill, fill, fill)
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

bool DepthSettings::automatic() const
{
    return d->o.automatic;
}

void DepthSettings::setAutomatic(bool value)
{
    stereo_dibr_options next = d->o;
    next.automatic = value ? 1 : 0;
    if (!value) {
        // the way that is drawn becomes the person's
        next.warp = shownWarp();
        next.fill = shownFill();
    } else {
        // the way is the library's again
        stereo_dibr_options def;
        stereo_dibr_options_default(&def);
        next.warp = def.warp;
        next.fill = def.fill;
    }
    if (!memcmp(&next, &d->o, sizeof(next)))
        return;
    d->o = next;
    Q_EMIT changed();
}

int DepthSettings::shownWarp() const
{
    return d->o.automatic && d->hasChoice ? d->warp : d->o.warp;
}

int DepthSettings::shownFill() const
{
    return d->o.automatic && d->hasChoice ? d->fill : d->o.fill;
}

void DepthSettings::useWay(int warp, int fill)
{
    stereo_dibr_options next = d->o;
    next.warp = warp;
    next.fill = fill;
    next.automatic = 0;
    stereo_dibr_options_clamp(&next);
    if (!memcmp(&next, &d->o, sizeof(next)))
        return;
    d->o = next;
    Q_EMIT changed();
}

void DepthSettings::setGpuMemory(const QVariantMap &memory)
{
    const double freeBytes = memory.contains(QStringLiteral("freeBytes")) ? memory.value(QStringLiteral("freeBytes")).toDouble() : -1;
    const int w = memory.value(QStringLiteral("width"), 0).toInt(), h = memory.value(QStringLiteral("height"), 0).toInt();
    const int bits = memory.value(QStringLiteral("bits"), 8).toInt();
    if (freeBytes == d->freeBytes && w == d->memWidth && h == d->memHeight && bits == d->memBits)
        return;
    d->freeBytes = freeBytes;
    d->memWidth = w;
    d->memHeight = h;
    d->memBits = bits;
    Q_EMIT changed();
}

QVariantMap DepthSettings::gpuMemory() const
{
    QVariantMap map;
    if (d->freeBytes >= 0 && d->memWidth > 0) {
        map.insert(QStringLiteral("freeBytes"), d->freeBytes);
        map.insert(QStringLiteral("width"), d->memWidth);
        map.insert(QStringLiteral("height"), d->memHeight);
        map.insert(QStringLiteral("bits"), d->memBits);
    }
    return map;
}

QString DepthSettings::wayText(const QString &key, int warp, int fill) const
{
    const QString text = label(key);
    // the guard of the automatic choice and this sentence are one rule: stereo_dibr_way_fits()
    if (d->freeBytes < 0 || d->memWidth <= 0 || stereo_dibr_way_fits(warp, fill, d->memWidth, d->memHeight, d->memBits, d->freeBytes))
        return text;
    static const char *context = "Stereo::DepthControls";
    const double need = stereo_dibr_way_memory(warp, fill, d->memWidth, d->memHeight, d->memBits);
    return QCoreApplication::translate(context, "%1 (needs about %2 MB of the GPU's memory, %3 MB is free)")
        .arg(text, QString::number(qRound(need / 1e6)), QString::number(qRound(d->freeBytes / 1e6)));
}

void DepthSettings::setChoice(const QVariantMap &choice)
{
    const bool had = d->hasChoice;
    const int warp = d->warp, fill = d->fill, sw = d->skippedWarp, sf = d->skippedFill, fps = d->fps;
    const bool keeps = d->keepsUp, gpu = d->onGpu, lost = d->gpuLost, memory = d->skippedMemory;
    d->hasChoice = choice.contains(QStringLiteral("warp")) && choice.contains(QStringLiteral("fill"));
    if (d->hasChoice) {
        d->warp = choice.value(QStringLiteral("warp")).toInt();
        d->fill = choice.value(QStringLiteral("fill")).toInt();
        d->skippedWarp = choice.value(QStringLiteral("skippedWarp"), -1).toInt();
        d->skippedFill = choice.value(QStringLiteral("skippedFill"), -1).toInt();
        d->fps = qRound(choice.value(QStringLiteral("fps"), 0).toDouble());
        d->keepsUp = choice.value(QStringLiteral("keepsUp"), true).toBool();
        d->onGpu = choice.value(QStringLiteral("onGpu"), true).toBool();
        d->gpuLost = choice.value(QStringLiteral("gpuLost"), false).toBool();
        // -2: the way was not run, the GPU has not the memory for it
        d->skippedMemory = choice.value(QStringLiteral("skippedMs"), 0).toDouble() < -1.5;
    }
    if (had != d->hasChoice || (d->hasChoice && (warp != d->warp || fill != d->fill || sw != d->skippedWarp || sf != d->skippedFill || fps != d->fps
                                                  || keeps != d->keepsUp || gpu != d->onGpu || lost != d->gpuLost || memory != d->skippedMemory)))
        Q_EMIT changed();
}

QString DepthSettings::chosenText() const
{
    if (!d->o.automatic || !d->hasChoice)
        return QString();
    static const char *context = "Stereo::DepthControls";
    auto way = [](int warp, int fill) {
        const char *w = warp == STEREO_DIBR_WARP_LAYERED ? "warpLayered" : warp == STEREO_DIBR_WARP_BACKWARD ? "warpBackward" : "warpForward";
        const char *f = fill == STEREO_DIBR_FILL_DEPTH_AIDED ? "fillDepthAided"
            : fill == STEREO_DIBR_FILL_PYRAMID               ? "fillPyramid"
            : fill == STEREO_DIBR_FILL_TELEA                 ? "fillTelea"
            : fill == STEREO_DIBR_FILL_NONE                  ? "fillNone"
                                                             : "fillBackground";
        return DepthSettings::label(QLatin1String(w)) + QStringLiteral(", ") + DepthSettings::label(QLatin1String(f));
    };
    const QString where = d->onGpu ? QCoreApplication::translate(context, "on this GPU") : QCoreApplication::translate(context, "on this processor");
    const QString rate = QString::number(d->fps);
    const QString chosen = way(d->warp, d->fill);
    // the GPU was reset (a driver or hardware fault): the GPU is not used again for this video, and it is said, never silently
    if (d->gpuLost)
        return QCoreApplication::translate(context, "%1 (the GPU was reset while drawing: the processor draws now)").arg(chosen);
    if (!d->keepsUp)
        return QCoreApplication::translate(context, "%1 (the quickest: no way keeps %2 frames a second %3)").arg(chosen, rate, where);
    if (d->skippedMemory && d->skippedWarp >= 0 && d->skippedFill >= 0)
        return QCoreApplication::translate(context, "%1 (%2 needs more of the GPU's memory than is free)").arg(chosen, way(d->skippedWarp, d->skippedFill));
    if (d->skippedWarp >= 0 && d->skippedFill >= 0)
        return QCoreApplication::translate(context, "%1 (%2 does not keep %3 frames a second %4)")
            .arg(chosen, way(d->skippedWarp, d->skippedFill), rate, where);
    return QCoreApplication::translate(context, "%1 (keeps %2 frames a second %3)").arg(chosen, rate, where);
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
        : d->o.fill == STEREO_DIBR_FILL_PYRAMID          ? Picture::DepthOptions::Fill::Pyramid
                                                         : Picture::DepthOptions::Fill::Background;
    if (d->o.automatic) {
        // a still picture, and the saving into a file, have no time limit: the best way there is
        out.warp = Picture::DepthOptions::Warp::Layered;
        out.fill = Picture::DepthOptions::Fill::DepthAided;
        stereo_dibr_options def;
        stereo_dibr_options_default(&def);
        out.warp = def.warp == STEREO_DIBR_WARP_BACKWARD ? Picture::DepthOptions::Warp::Backward
            : def.warp == STEREO_DIBR_WARP_LAYERED       ? Picture::DepthOptions::Warp::Layered
                                                         : Picture::DepthOptions::Warp::Forward;
        out.fill = def.fill == STEREO_DIBR_FILL_PYRAMID ? Picture::DepthOptions::Fill::Pyramid
            : def.fill == STEREO_DIBR_FILL_TELEA        ? Picture::DepthOptions::Fill::Telea
            : def.fill == STEREO_DIBR_FILL_BACKGROUND   ? Picture::DepthOptions::Fill::Background
            : def.fill == STEREO_DIBR_FILL_NONE         ? Picture::DepthOptions::Fill::None
                                                        : Picture::DepthOptions::Fill::DepthAided;
    }
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
        {"fillPyramid", QCoreApplication::translate(context, "Pyramid")},
        {"automatic", QCoreApplication::translate(context, "Automatic")},
        {"automaticTip",
         QCoreApplication::translate(context,
                                     "A still picture and a picture saved into a file are drawn the best way there is. A video is drawn the best way that keeps "
                                     "its frame rate on this machine, timed once for each GPU and picture size. Choosing a warping or a gap filling yourself "
                                     "turns this off: your choice then holds for everything, the slowest way included.")},
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
