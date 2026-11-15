// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "stereo_layer_export.h"
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
class QQuickItem;
class QQuickWindow;
namespace Stereo
{
class DepthWindow;
// Where an item of a Qt Quick window sits in depth on a stereo output, in the words the desktop uses for its own surfaces: the
// screen, popped in front of it, sunk behind it (Daniel, 2026-10-05), as a fraction of the person's limits, the ones Stereo KWin
// gives the output the window mainly sits on (kde_stereo_depth_v1.depth_limits: the person's PoppedLimit and SunkLimit of kwinrc
// [StereoDepth], built on that display). 1 is the popped limit, -1 the sunk limit, 0 the screen; anything between is valid, nothing
// beyond. The depth is absolute, from the screen plane: an item stands at its own depth whatever its parent's.
//
// While the window's surface is awake on a stereo output (kde_stereo_depth_v1.depth_state) and the limits are not both zero, the
// window's _q_stereo_depth is set and each item gets its _q_stereo_parallax, so the edition's Qt Quick draws the window once per eye,
// each item moved by its parallax, and hands the pair over as the desktop's own depth. A parallax is rounded to whole device pixels,
// half away from zero, as Stereo KWin rounds a surface's level and the boot splash rounds its levels: a picture keeps its pixels in
// both eyes. On a 2D output, asleep, at limits of zero, or on a compositor that does not tell a surface its depth, the window is
// drawn once, exactly as without depth.
//
// The type does not apply the stereo window rule to an item: a popped item keeps away from the output's edges by itself, as the
// desktop keeps its popups 10 pixels inside them.
class STEREO_LAYER_EXPORT DepthAttached : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(qreal depth READ depth WRITE setDepth NOTIFY depthChanged)
    // the parallax the item is drawn with now, device pixels of the output, positive in front of the screen; 0 while depth is not shown
    Q_PROPERTY(qreal parallax READ parallax NOTIFY parallaxChanged)
    // the window shows its items' depth now
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    // the person's limits for the window's output, in pixels of a view 1920 wide, as Stereo KWin sends them; 0 until they arrive
    Q_PROPERTY(qreal poppedLimit READ poppedLimit NOTIFY limitsChanged)
    Q_PROPERTY(qreal sunkLimit READ sunkLimit NOTIFY limitsChanged)
public:
    explicit DepthAttached(QQuickItem *item);
    ~DepthAttached() override;
    qreal depth() const { return m_depth; }
    void setDepth(qreal depth);
    qreal parallax() const { return m_parallax; }
    bool shown() const;
    qreal poppedLimit() const;
    qreal sunkLimit() const;
    // the depth turned into the parallax of the window's output and handed to Qt Quick; called by the window when its state changes
    void update();
    QQuickItem *item() const { return m_item; }
Q_SIGNALS:
    void depthChanged();
    void parallaxChanged();
    void shownChanged();
    void limitsChanged();

private:
    void join(QQuickWindow *window);
    QQuickItem *m_item = nullptr;
    QPointer<DepthWindow> m_window;
    qreal m_depth = 0;
    qreal m_parallax = 0;
    bool m_set = false;
    bool m_shown = false;
};
class STEREO_LAYER_EXPORT Depth : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StereoDepth)
    QML_UNCREATABLE("StereoDepth is an attached property: StereoDepth.depth")
    QML_ATTACHED(Stereo::DepthAttached)
public:
    enum Level { Sunk = -1, Screen = 0, Popped = 1 };
    Q_ENUM(Level)
    static DepthAttached *qmlAttachedProperties(QObject *object);
    // The parallax of a depth in device pixels of an output whose eye is viewWidth wide: the fraction of the popped limit (depth 0 to
    // 1) or of the sunk limit (depth -1 to 0), limits in pixels of a view 1920 wide, rounded half away from zero to whole pixels.
    Q_INVOKABLE static qreal parallaxFor(qreal depth, qreal popped, qreal sunk, qreal viewWidth);
};
}
