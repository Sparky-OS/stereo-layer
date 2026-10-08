// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "widgetlayer.h"
#include "render.h"
#include "surface_p.h"
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QWidget>
#include <QWindow>
namespace Stereo
{
namespace
{
class WindowBackground : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() != QEvent::Paint)
            return false;
        auto* window = static_cast<QWidget*>(object);
        QRegion region = static_cast<QPaintEvent*>(event)->region();
        for (auto* widget : window->findChildren<QWidget*>()) {
            if (widget->property("stereoLayerActive").toBool() && widget->isVisible())
                region -= QRect(widget->mapTo(window, QPoint()), widget->size());
        }
        QPainter painter(window);
        painter.setClipRegion(region);
        painter.fillRect(window->rect(), window->palette().brush(window->backgroundRole()));
        QStyleOption option;
        option.initFrom(window);
        window->style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, window);
        return false;
    }
};
}
struct WidgetLayer::Private {
    QPointer<QWidget> area;
    Picture picture;
    Surface surface;
    QTimer timer;
    qreal zoom = 1;
    QPointF pan;
    bool active = false;
    bool paintArea = true;
    QImage packed;
    QSize pixels;
    bool dirty = true;
};
WidgetLayer::WidgetLayer(QWidget* area, QObject* parent)
    : QObject(parent ? parent : area)
    , d(std::make_unique<Private>())
{
    d->area = area;
    area->setAutoFillBackground(false);
    area->installEventFilter(this);
    connect(area, &QObject::destroyed, this, [this] { d->surface.reset(); });
    d->timer.setInterval(33);
    connect(&d->timer, &QTimer::timeout, this, &WidgetLayer::update);
    d->timer.start();
}
WidgetLayer::~WidgetLayer()
{
    if (d->area) {
        d->area->setProperty("stereoLayerActive", false);
        d->area->window()->update();
    }
}
bool WidgetLayer::prepareWindow(QWidget* window)
{
    if (window->windowHandle() || window->testAttribute(Qt::WA_WState_Created))
        return false;
    if (!isSupported())
        return false;
    if (window->property("stereoLayerPrepared").toBool())
        return true;
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setProperty("stereoLayerPrepared", true);
    window->installEventFilter(new WindowBackground(window));
    return true;
}
bool WidgetLayer::isSupported() { return Surface::isSupported(); }
void WidgetLayer::setPicture(const Picture& picture)
{
    d->picture = picture;
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
void WidgetLayer::setTransform(qreal zoom, const QPointF& pan)
{
    d->zoom = zoom;
    d->pan = pan;
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
void WidgetLayer::setPaintArea(bool enabled) { d->paintArea = enabled; }
bool WidgetLayer::isActive() const { return d->active; }
void WidgetLayer::paint(QPainter* painter)
{
    if (!d->area)
        return;
    if (d->active) {
        painter->setCompositionMode(QPainter::CompositionMode_Source);
        painter->fillRect(d->area->rect(), Qt::transparent);
    } else {
        const QSize pixels = d->area->size();
        painter->drawImage(d->area->rect(), renderMono(d->picture.original, pixels, d->zoom, d->pan));
    }
}
bool WidgetLayer::eventFilter(QObject* object, QEvent* event)
{
    if (object == d->area && event->type() == QEvent::Paint && d->paintArea) {
        QPainter painter(d->area);
        paint(&painter);
        return true;
    }
    if (event->type() == QEvent::Hide || event->type() == QEvent::Show || event->type() == QEvent::Resize || event->type() == QEvent::Move
        || event->type() == QEvent::ParentChange) {
        QTimer::singleShot(0, this, &WidgetLayer::update);
    }
    return false;
}
void WidgetLayer::update()
{
    const bool visible = d->area && d->area->isVisible() && !d->area->size().isEmpty();
    bool active = false;
    if (visible && d->picture.isStereo() && d->area->window()->testAttribute(Qt::WA_TranslucentBackground)) {
        const qreal ratio = d->area->devicePixelRatioF();
        const QSize pixels(qRound(d->area->width() * ratio), qRound(d->area->height() * ratio));
        if (d->dirty || pixels != d->pixels) {
            d->packed = renderEyes(d->picture, pixels, d->zoom, d->pan * ratio);
            d->pixels = pixels;
            d->dirty = false;
        }
        active = d->surface.show(d->area->window()->windowHandle(), QRect(d->area->mapTo(d->area->window(), QPoint()), d->area->size()), d->packed);
    } else if (!visible) {
        d->surface.hide();
    } else {
        d->surface.reset();
    }
    if (active != d->active) {
        d->active = active;
        if (d->area) {
            d->area->setProperty("stereoLayerActive", active);
            d->area->update();
            d->area->window()->update();
        }
        Q_EMIT activeChanged();
    }
}
}
