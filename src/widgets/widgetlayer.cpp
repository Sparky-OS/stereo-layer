// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "widgetlayer.h"
#include "render.h"
#include "screenmode.h"
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
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Paint)
            return false;
        auto *window = static_cast<QWidget *>(object);
        QRegion region = static_cast<QPaintEvent *>(event)->region();
        for (auto *widget : window->findChildren<QWidget *>()) {
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
    QString source;
    Picture::Options options;
    ScreenMode output;
    QPointer<QWindow> window;
    Surface surface;
    QTimer timer;
    qreal zoom = 1;
    QPointF pan;
    bool active = false;
    bool paintArea = true;
    bool keepTransparency = false;
    QImage packed;
    QSize pixels;
    bool dirty = true;
};
WidgetLayer::WidgetLayer(QWidget *area, QObject *parent)
    : QObject(parent ? parent : area)
    , d(std::make_unique<Private>())
{
    d->area = area;
    area->setAutoFillBackground(false);
    area->installEventFilter(this);
    connect(area, &QObject::destroyed, this, [this] { d->surface.reset(); });
    connect(&d->output, &ScreenMode::modeChanged, this, [this] {
        if (!d->source.isEmpty()) {
            d->picture = Picture::load(d->source, d->options, d->output.mode());
            d->dirty = true;
            update();
            if (d->area)
                d->area->update();
        }
    });
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
bool WidgetLayer::prepareWindow(QWidget *window)
{
    if (window->property("stereoLayerPrepared").toBool())
        return true;
    if (window->windowHandle() || window->testAttribute(Qt::WA_WState_Created))
        return false;
    if (!isSupported())
        return false;
    if (window->property("stereoLayerPrepared").toBool())
        return true;
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setProperty("stereoLayerPrepared", true);
    // A tooltip or a popup draws its own panel, with the shape its style gives it (rounded corners, a shadow)
    if (window->windowType() == Qt::ToolTip || window->windowType() == Qt::Popup)
        return true;
    window->installEventFilter(new WindowBackground(window));
    return true;
}
bool WidgetLayer::isSupported() { return Surface::isSupported(); }
void WidgetLayer::setPicture(const Picture &picture)
{
    d->source.clear();
    d->picture = picture;
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
void WidgetLayer::setSource(const QString &path, const Picture::Options &options)
{
    d->source = path;
    d->options = options;
    d->picture = Picture::load(path, options, d->output.mode());
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
void WidgetLayer::setTransform(qreal zoom, const QPointF &pan)
{
    d->zoom = zoom;
    d->pan = pan;
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
void WidgetLayer::setPaintArea(bool enabled) { d->paintArea = enabled; }
void WidgetLayer::setKeepTransparency(bool keep)
{
    d->keepTransparency = keep;
    d->dirty = true;
    update();
}
bool WidgetLayer::isActive() const { return d->active; }
bool WidgetLayer::approximateColor() const { return d->picture.approximateColor; }
void WidgetLayer::paint(QPainter *painter)
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
bool WidgetLayer::eventFilter(QObject *object, QEvent *event)
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
    QWindow *window = d->area ? d->area->window()->windowHandle() : nullptr;
    if (window != d->window) {
        if (d->window)
            disconnect(d->window, nullptr, &d->output, nullptr);
        d->window = window;
        if (window)
            connect(window, &QWindow::screenChanged, &d->output, &ScreenMode::setScreen);
        d->output.setScreen(window ? window->screen() : nullptr);
    }
    const bool visible = d->area && d->area->isVisible() && !d->area->size().isEmpty();
    bool active = false;
    if (visible && d->picture.isStereo() && d->area->window()->testAttribute(Qt::WA_TranslucentBackground)) {
        const qreal ratio = d->area->devicePixelRatioF();
        const QSize pixels(qRound(d->area->width() * ratio), qRound(d->area->height() * ratio));
        if (d->dirty || pixels != d->pixels) {
            d->packed = renderEyes(d->picture, pixels, d->zoom, d->pan * ratio);
            if (d->keepTransparency && d->zoom == 1 && d->pan.isNull() && (d->picture.left.hasAlphaChannel() || d->picture.right.hasAlphaChannel())) {
                // Each eye as it is, clear parts included, at the area's size
                QImage packed(pixels.width() * 2, pixels.height(), QImage::Format_ARGB32_Premultiplied);
                packed.fill(Qt::transparent);
                QPainter painter(&packed);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(QRect(QPoint(), pixels), d->picture.left);
                painter.drawImage(QRect(QPoint(pixels.width(), 0), pixels), d->picture.right);
                d->packed = packed;
            }
            d->pixels = pixels;
            d->dirty = false;
        }
        active = d->surface.show(d->area->window()->windowHandle(), QRect(d->area->mapTo(d->area->window(), QPoint()), d->area->size()), d->packed);
    } else if (!visible) {
        d->surface.reset();
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
