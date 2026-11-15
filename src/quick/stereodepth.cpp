// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "stereodepth.h"
#include "surface_p.h"
#include <QAbstractEventDispatcher>
#include <QSGRendererInterface>
#include <algorithm>
#include <QGuiApplication>
#include <QHash>
#include <QLoggingCategory>
#include <QPlatformSurfaceEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <cmath>
#include <qpa/qplatformnativeinterface.h>
#include <stereo-declare.h>
#include <wayland-client.h>
// the library's header includes Xlib, which defines names Qt uses
#undef Expose
#undef None
#undef Bool
#undef Status
#undef Unsorted

Q_LOGGING_CATEGORY(qLcDepth, "org.sparky.stereo.depth")

namespace Stereo
{
// The names the edition's Qt Quick reads (qt6-declarative, "Quick: draw a window whose elements show depth once per eye")
static constexpr char DepthProperty[] = "_q_stereo_depth";
static constexpr char ViewsProperty[] = "_q_stereo_views";
static constexpr char PairProperty[] = "_q_stereo_pair";
static constexpr char ParallaxProperty[] = "_q_stereo_parallax";
// a window whose items show depth and that is still drawn as one picture after this many frames is said to be flat
static constexpr int FramesBeforeFlat = 30;

// What one window shows: its surface's state and limits from Stereo KWin, followed through the declaration library on the queue
// the layer's surfaces share, and the items of the window that have a depth.
class DepthWindow : public QObject
{
public:
    static DepthWindow *of(QQuickWindow *window)
    {
        auto it = windows().constFind(window);
        return it != windows().cend() ? *it : new DepthWindow(window);
    }
    ~DepthWindow() override
    {
        // the window is being destroyed: its properties are not touched any more
        m_firstState.stop();
        if (m_surface)
            stereo_depth_wayland_unwatch(m_surface);
        windows().remove(m_window);
    }
    void add(DepthAttached *item)
    {
        if (!m_items.contains(item))
            m_items.append(item);
        item->update();
    }
    void remove(DepthAttached *item) { m_items.removeAll(item); }
    bool shown() const { return m_shown; }
    qreal popped() const { return m_popped; }
    qreal sunk() const { return m_sunk; }
    // the width Stereo KWin draws each eye of the window's output at, in device pixels: the limits are pixels of a view 1920 wide.
    // The window's own ratio, not the screen's: on Wayland the screen's is wl_output's whole scale (2 at 125 %), the window's the
    // fractional scale the output is drawn at (1.25), the one Qt Quick turns a parallax in device pixels into the scene's units with
    qreal viewWidth() const
    {
        const QScreen *screen = m_window->screen();
        return screen ? screen->geometry().width() * m_window->devicePixelRatio() : 1920;
    }

private:
    explicit DepthWindow(QQuickWindow *window)
        : QObject(window)
        , m_window(window)
        , m_connection(sharedConnection())
    {
        windows().insert(window, this);
        window->installEventFilter(this);
        connect(window, &QWindow::screenChanged, this, &DepthWindow::screenChanged);
        screenChanged(window->screen());
        connect(window, &QQuickWindow::frameSwapped, this, &DepthWindow::frameSwapped, Qt::QueuedConnection);
        if (!m_connection.queue) {
            if (!m_connection.failure.isEmpty())
                qCWarning(qLcDepth).noquote() << "items of" << window << "are drawn flat:" << m_connection.failure;
            return;
        }
        // the library's events arrive on the layer's queue, read with the display's other events: dispatched before the loop sleeps,
        // and every 100 ms until the first state and limits are in, so a window that draws nothing still hears them
        m_dispatch = connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock, this, &DepthWindow::dispatch);
        m_firstState.setInterval(100);
        connect(&m_firstState, &QTimer::timeout, this, &DepthWindow::dispatch);
        watch();
    }
    static QHash<const QQuickWindow *, DepthWindow *> &windows()
    {
        static QHash<const QQuickWindow *, DepthWindow *> all;
        return all;
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window) {
            if (event->type() == QEvent::PlatformSurface) {
                if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                    unwatch();
            } else if (event->type() == QEvent::DevicePixelRatioChange) {
                itemsChanged();
            } else if (event->type() == QEvent::Expose && !m_surface) {
                watch(); // the surface exists once the window is shown
            } else if (event->type() == QEvent::DynamicPropertyChange
                       && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == PairProperty) {
                if (m_window->property(PairProperty).toBool() && m_toldFlat) {
                    qCInfo(qLcDepth) << "items of" << m_window << "are drawn as a pair now";
                    m_toldFlat = false;
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }
    void screenChanged(QScreen *screen)
    {
        disconnect(m_screenGeometry);
        if (screen)
            m_screenGeometry = connect(screen, &QScreen::geometryChanged, this, &DepthWindow::itemsChanged);
        itemsChanged();
    }
    void watch()
    {
        if (!m_connection.queue || m_surface)
            return;
        auto native = QGuiApplication::platformNativeInterface();
        auto surface = native ? static_cast<wl_surface *>(native->nativeResourceForWindow("surface", m_window)) : nullptr;
        if (!surface)
            return;
        if (!stereo_depth_supported_wayland()) {
            qCWarning(qLcDepth) << "items of" << m_window << "are drawn flat: the compositor does not tell a surface its depth (kde_stereo_depth_v1)";
            return;
        }
        if (stereo_depth_wayland_watch(surface, &DepthWindow::onState, this) != 0)
            return;
        m_surface = surface;
        const int limits = stereo_depth_wayland_watch_limits(surface, &DepthWindow::onLimits, this);
        m_limitsOffered = limits != -ENOTSUP;
        if (!m_limitsOffered)
            qCWarning(qLcDepth) << "items of" << m_window << "are drawn flat: the compositor does not send the person's depth limits (kde_stereo_depth_v1 version 5)";
        stereo_depth_limits read;
        if (limits == 0 && stereo_depth_wayland_get_limits(surface, &read) == 0)
            setLimits(read.sunk, read.popped);
        wl_display_flush(m_connection.display);
        m_firstState.start();
    }
    void unwatch()
    {
        m_firstState.stop();
        if (m_surface)
            stereo_depth_wayland_unwatch(m_surface);
        m_surface = nullptr;
        m_awake = false;
        apply();
    }
    // the first state and limits are in (or the compositor sends no limits): the events are heard before the loop sleeps from now on
    void firstStateIn()
    {
        if (m_hasState && (m_hasLimits || !m_limitsOffered))
            m_firstState.stop();
    }
    void dispatch()
    {
        if (m_connection.display)
            wl_display_dispatch_queue_pending(m_connection.display, m_connection.queue);
    }
    static void onState(void *data, wl_surface *surface, const stereo_depth_state *state)
    {
        auto self = static_cast<DepthWindow *>(data);
        if (surface != self->m_surface)
            return;
        self->m_hasState = true;
        self->m_awake = state->awake != 0;
        self->firstStateIn();
        self->apply();
    }
    static void onLimits(void *data, wl_surface *surface, const stereo_depth_limits *limits)
    {
        auto self = static_cast<DepthWindow *>(data);
        if (surface == self->m_surface)
            self->setLimits(limits->sunk, limits->popped);
    }
    void setLimits(qreal sunk, qreal popped)
    {
        m_hasLimits = true;
        firstStateIn();
        if (sunk == m_sunk && popped == m_popped)
            return;
        m_sunk = sunk;
        m_popped = popped;
        for (const auto &item : std::as_const(m_items))
            if (item)
                Q_EMIT item->limitsChanged();
        apply();
    }
    // the window shows depth while it is awake and the limits are not both zero
    void apply()
    {
        const bool shown = m_surface && m_awake && m_hasLimits && (m_popped > 0 || m_sunk > 0);
        if (shown != m_shown) {
            m_shown = shown;
            m_framesWithoutPair = 0;
            m_window->setProperty(DepthProperty, shown);
            if (shown && m_window->property(ViewsProperty).toInt() != 2 && !m_toldFlat) {
                qCWarning(qLcDepth) << "items of" << m_window
                                    << "are drawn flat: this Qt Quick does not draw a window once per eye (the edition's qt6-declarative does)";
                m_toldFlat = true;
            }
        }
        itemsChanged();
    }
    void itemsChanged()
    {
        for (const auto &item : std::as_const(m_items))
            if (item)
                item->update();
    }
    void frameSwapped()
    {
        if (!m_shown || m_toldFlat || m_window->property(PairProperty).toBool() || ++m_framesWithoutPair < FramesBeforeFlat)
            return;
        const bool software = m_window->rendererInterface() && m_window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
        qCWarning(qLcDepth).noquote() << "items of" << m_window << "are drawn flat:"
                                      << (software ? QStringLiteral("Qt Quick's software renderer draws one picture")
                                                   : QStringLiteral("the window's surface was not given two views (EGL_EXT_multiview_window or a two-layer swapchain)"));
        m_toldFlat = true;
    }
    QQuickWindow *m_window;
    SharedConnection m_connection;
    wl_surface *m_surface = nullptr;
    bool m_hasState = false;
    bool m_awake = false;
    bool m_hasLimits = false;
    bool m_limitsOffered = true;
    qreal m_popped = 0;
    qreal m_sunk = 0;
    bool m_shown = false;
    bool m_toldFlat = false;
    int m_framesWithoutPair = 0;
    QList<QPointer<DepthAttached>> m_items;
    QMetaObject::Connection m_dispatch;
    QMetaObject::Connection m_screenGeometry;
    QTimer m_firstState;
};

DepthAttached::DepthAttached(QQuickItem *item)
    : QObject(item)
    , m_item(item)
{
    connect(item, &QQuickItem::windowChanged, this, &DepthAttached::join);
}

DepthAttached::~DepthAttached()
{
    if (m_window)
        m_window->remove(this);
}

void DepthAttached::setDepth(qreal depth)
{
    depth = std::clamp(depth, qreal(-1), qreal(1));
    if (m_set && depth == m_depth)
        return;
    m_depth = depth;
    const bool first = !m_set;
    m_set = true;
    Q_EMIT depthChanged();
    if (first)
        join(m_item->window());
    else
        update();
}

bool DepthAttached::shown() const
{
    return m_shown;
}

qreal DepthAttached::poppedLimit() const
{
    return m_window ? m_window->popped() : 0;
}

qreal DepthAttached::sunkLimit() const
{
    return m_window ? m_window->sunk() : 0;
}

void DepthAttached::join(QQuickWindow *window)
{
    if (!m_set)
        return;
    if (m_window)
        m_window->remove(this);
    m_window = window ? DepthWindow::of(window) : nullptr;
    if (m_window)
        m_window->add(this);
    else
        update();
    Q_EMIT limitsChanged();
}

void DepthAttached::update()
{
    const bool shown = m_window && m_window->shown();
    qreal parallax = 0;
    if (shown)
        parallax = Depth::parallaxFor(m_depth, m_window->popped(), m_window->sunk(), m_window->viewWidth());
    // Qt Quick reads the item's parallax as a dynamic property, set only while the window shows depth
    m_item->setProperty(ParallaxProperty, shown ? QVariant(parallax) : QVariant());
    if (parallax != m_parallax) {
        m_parallax = parallax;
        Q_EMIT parallaxChanged();
    }
    if (shown != m_shown) {
        m_shown = shown;
        Q_EMIT shownChanged();
    }
}

qreal Depth::parallaxFor(qreal depth, qreal popped, qreal sunk, qreal viewWidth)
{
    depth = std::clamp(depth, qreal(-1), qreal(1));
    const qreal limit = depth >= 0 ? popped : sunk;
    return std::round(depth * limit * viewWidth / 1920);
}

DepthAttached *Depth::qmlAttachedProperties(QObject *object)
{
    auto item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        qCWarning(qLcDepth) << "StereoDepth is attached to an Item, not to" << object;
        return nullptr;
    }
    return new DepthAttached(item);
}
}
