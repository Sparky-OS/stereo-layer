// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <memory>
class QWindow;
struct wl_display;
struct wl_event_queue;
namespace Stereo
{
// The connection the layer's surfaces share, for the layer's code that follows a window's depth (StereoDepth): the declaration
// library's state belongs to the thread and is bound to this queue, so whoever watches a surface through it dispatches this queue.
// display and queue are null on a compositor that announces no stereo global, or when the library could not start (failure says why).
struct SharedConnection {
    wl_display *display = nullptr;
    wl_event_queue *queue = nullptr;
    QString failure;
    std::shared_ptr<void> keep;
};
SharedConnection sharedConnection();

class Surface
{
public:
    Surface();
    ~Surface();
    static bool isSupported();
    // rect is where the picture shows, the window's hole (see covering()); with exact, rect and packed are those of onWholePixels() and
    // nothing is resized
    bool show(QWindow *parent, const QRect &rect, const QImage &packed, bool exact = false, bool above = false);
    // A window paints an edge at the nearest whole device pixel, counted from the corner of its content. A compositor places a surface in the
    // window's own whole pixels and rounds it to device pixels by a rule fractional-scale-v1 does not define (wayland-protocols issue 132; KDE
    // bug 491725): KWin rounds a subsurface's corner from its parent's and its size on its own, others round both edges. At a scale that is not
    // whole the two can differ by a device pixel, and then a hole the window leaves and the picture under it are out of step.
    //
    // wholePixelStep(): the step, in the window's own pixels, between places that are whole device pixels at this scale (1 at a whole scale, 4 at
    // 125 % and 175 %, 2 at 150 %; a compositor gives scales in 120ths), or 0 when the scale is no number of 120ths.
    static int wholePixelStep(qreal ratio);
    // onWholePixels(): rect (the window's own coordinates) grown outward to that step, or to the corner of the window's content when there is no
    // step; pixels gets the device pixels it covers, counted from the content's corner. A surface placed there lies on whole device pixels from
    // corner to corner under every rounding, also under a frame the window draws itself, so a buffer of exactly those pixels is shown one to one.
    static QRect onWholePixels(const QWindow *parent, const QRect &rect, QRect *pixels);
    // covering(): for a picture the compositor resizes into rect: rect itself, or rect a pixel of the window larger where that is needed for its
    // device pixels, under either rounding, to hold every device pixel the window leaves clear for rect. The picture then fills its hole at every
    // scale; what is over, a device pixel at most, lies under the window.
    static QRect covering(const QWindow *parent, const QRect &rect);
    void hide();
    void reset();
    // Empty, or why the compositor did not take the surface as stereo content (the one message every player shows): it announces the stereo
    // global but the declaration could not be made or was refused. A compositor with no stereo global is not a failure (a 2D desktop). It stays
    // until reset(); show() returns false without trying again while it does.
    QString failure() const;

private:
    void fail(const QString &what);
    struct Private;
    std::unique_ptr<Private> d;
};
}
