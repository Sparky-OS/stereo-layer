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
            if (widget->property("stereoLayerActive").toBool() && widget->isVisible()) {
                // the hole is where the picture shows, the rest of the area is the window's own
                const QRect shown = widget->property("stereoLayerShown").toRect();
                region -= (shown.isValid() ? shown : widget->rect()).translated(widget->mapTo(window, QPoint()));
            }
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
    QString failure;
    bool paintArea = true;
    bool keepTransparency = false;
    QImage packed;
    QRect shown; // where the picture shows in the area, the rest of it the window's own
    QRect announced; // shown while the layer is active, what shownRect() says and the area's property holds
    QSize pixels;
    bool dirty = true;
    // eyes drawn by the adapter on the window's own pixels (setEyes()), and what they were drawn for
    bool eyes = false;
    QRect eyePixels;
    QRect eyeRect; // where their surface lies in the window, on whole device pixels
    qreal eyeRatio = 0;
    bool eyesAsked = false;
    QPointer<QWidget> watched; // the area's window, whose paint and geometry the layer follows too
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
        d->area->setProperty("stereoLayerShown", QRect());
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
    d->eyes = false;
    d->picture = picture;
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
void WidgetLayer::setSource(const QString &path, const Picture::Options &options)
{
    d->source = path;
    d->eyes = false;
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
QPoint WidgetLayer::eyeOffset() const { return d->area ? d->area->mapTo(d->area->window(), QPoint()) : QPoint(); }
QImage WidgetLayer::eyeCanvas() const
{
    QWindow *window = d->area ? d->area->window()->windowHandle() : nullptr;
    if (!window || d->area->size().isEmpty())
        return {};
    QRect pixels;
    Surface::onWholePixels(window, QRect(eyeOffset(), d->area->size()), &pixels);
    QImage canvas(pixels.right() + 1, pixels.bottom() + 1, QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull())
        return {};
    canvas.setDevicePixelRatio(d->area->devicePixelRatioF());
    canvas.fill(Qt::transparent);
    return canvas;
}
void WidgetLayer::setEyes(const QImage &left, const QImage &right)
{
    d->source.clear();
    d->picture = Picture();
    d->packed = QImage();
    d->eyes = true;
    d->eyesAsked = false;
    d->eyePixels = QRect();
    QWindow *window = d->area ? d->area->window()->windowHandle() : nullptr;
    QRect pixels;
    if (window && !left.isNull() && left.size() == right.size()) {
        d->eyeRect = Surface::onWholePixels(window, QRect(eyeOffset(), d->area->size()), &pixels);
        // canvases of eyeCanvas(), drawn for the area as it is now
        if (left.size() == QSize(pixels.right() + 1, pixels.bottom() + 1) && !pixels.isEmpty()) {
            // the surface's own pixels, from whole pixel to whole pixel around the area; clear where the area is not, and where it is kept clear
            d->packed = QImage(2 * pixels.width(), pixels.height(), QImage::Format_ARGB32_Premultiplied);
            d->packed.fill(Qt::transparent);
            QPainter painter(&d->packed);
            const QRect target(QPoint(), pixels.size());
            if (!d->keepTransparency) {
                // the clear parts of the area black, as around a picture
                QRect areaPixels;
                const qreal ratio = d->area->devicePixelRatioF();
                const QRect area(eyeOffset(), d->area->size());
                areaPixels = QRect(QPoint(qRound(area.x() * ratio), qRound(area.y() * ratio)),
                    QPoint(qRound((area.x() + area.width()) * ratio) - 1, qRound((area.y() + area.height()) * ratio) - 1)).translated(-pixels.topLeft());
                painter.fillRect(areaPixels, Qt::black);
                painter.fillRect(areaPixels.translated(pixels.width(), 0), Qt::black);
            } else {
                painter.setCompositionMode(QPainter::CompositionMode_Source);
            }
            painter.drawImage(target, left, pixels);
            painter.drawImage(target.translated(pixels.width(), 0), right, pixels);
            painter.end();
            d->eyePixels = pixels;
            d->eyeRatio = d->area->devicePixelRatioF();
            d->picture.layout = Picture::Layout::SideBySide;
            // the picture only says that there are two eyes; what is shown is the packed pixels
            d->picture.left = d->packed.copy(0, 0, 1, 1);
            d->picture.right = d->picture.left;
        }
    }
    d->dirty = true;
    update();
    if (d->area)
        d->area->update();
}
bool WidgetLayer::eyesFit() const
{
    QWindow *window = d->area ? d->area->window()->windowHandle() : nullptr;
    if (!window || d->packed.isNull())
        return false;
    QRect pixels;
    const QRect rect = Surface::onWholePixels(window, QRect(eyeOffset(), d->area->size()), &pixels);
    return pixels == d->eyePixels && rect == d->eyeRect && d->area->devicePixelRatioF() == d->eyeRatio;
}
void WidgetLayer::dropStaleEyes()
{
    // drawn for another place, size or scale: no hole is painted over them, the adapter draws flat until its new eyes come
    d->surface.hide();
    if (d->active) {
        d->active = false;
        d->announced = QRect();
        if (d->area) {
            d->area->setProperty("stereoLayerActive", false);
            d->area->setProperty("stereoLayerShown", QRect());
        }
        Q_EMIT activeChanged();
        Q_EMIT shownChanged();
    }
    if (!d->eyesAsked) {
        d->eyesAsked = true;
        QMetaObject::invokeMethod(this, &WidgetLayer::eyesWanted, Qt::QueuedConnection);
    }
}
void WidgetLayer::setPaintArea(bool enabled) { d->paintArea = enabled; }
void WidgetLayer::setKeepTransparency(bool keep)
{
    d->keepTransparency = keep;
    d->dirty = true;
    update();
}
bool WidgetLayer::isActive() const { return d->active; }
QRect WidgetLayer::shownRect() const { return d->active ? d->shown : QRect(); }
QString WidgetLayer::failure() const { return d->failure; }
bool WidgetLayer::approximateColor() const { return d->picture.approximateColor; }
void WidgetLayer::paint(QPainter *painter)
{
    if (!d->area)
        return;
    if (d->active) {
        // the picture's surface lies below the window: a hole where it shows, its background around it
        painter->setCompositionMode(QPainter::CompositionMode_Source);
        painter->fillRect(d->area->rect(), d->keepTransparency ? Qt::transparent : Qt::black);
        painter->fillRect(d->shown, Qt::transparent);
    } else if (!d->eyes) {
        const QSize pixels = d->area->size();
        // drawn flat (the surface is not shown: a screen with one eye, or a declaration the compositor did not take), a pair shows its left view, as the
        // file's flat picture; never both views side by side
        const QImage &flat = d->picture.isStereo() ? d->picture.left : d->picture.original;
        painter->drawImage(d->area->rect(), renderMono(flat, pixels, d->zoom, d->pan));
    }
}
bool WidgetLayer::eventFilter(QObject *object, QEvent *event)
{
    // Before the window or the area paints: eyes drawn for another place are let go first, so that no frame is painted with
    // holes over them
    if (event->type() == QEvent::Paint && d->eyes && d->active && !eyesFit())
        dropStaleEyes();
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
    QWidget *top = d->area ? d->area->window() : nullptr;
    if (top != d->watched) {
        if (d->watched && d->watched != d->area)
            d->watched->removeEventFilter(this);
        d->watched = top;
        if (top && top != d->area)
            top->installEventFilter(this);
    }
    const bool visible = d->area && d->area->isVisible() && !d->area->size().isEmpty();
    bool active = false;
    if (visible && d->eyes && d->picture.isStereo() && d->area->window()->testAttribute(Qt::WA_TranslucentBackground)) {
        if (!eyesFit()) {
            dropStaleEyes();
        } else {
            const QRect whole(QPoint(), d->area->size());
            if (whole != d->shown) {
                d->shown = whole;
                d->area->update();
            }
            d->dirty = false;
            active = d->surface.show(d->area->window()->windowHandle(), d->eyeRect, d->packed, true);
        }
    } else if (visible && d->picture.isStereo() && d->area->window()->testAttribute(Qt::WA_TranslucentBackground)) {
        const qreal ratio = d->area->devicePixelRatioF();
        const QSize pixels(qRound(d->area->width() * ratio), qRound(d->area->height() * ratio));
        if (d->dirty || pixels != d->pixels) {
            // each eye's samples that show, at the resolution they have; the compositor resizes them once
            const EyeCrop crop = cropEyes(d->picture, pixels, d->zoom, d->pan * ratio);
            d->packed = crop.packed;
            if (!d->keepTransparency && d->packed.hasAlphaChannel()) {
                // the clear parts of the picture black, as around it
                QImage opaque(d->packed.size(), QImage::Format_RGB32);
                opaque.fill(Qt::black);
                QPainter painter(&opaque);
                painter.drawImage(QPoint(0, 0), d->packed);
                painter.end();
                d->packed = opaque;
            }
            const QRect shown(QPoint(qRound(crop.shown.left() / ratio), qRound(crop.shown.top() / ratio)),
                QPoint(qRound(crop.shown.right() / ratio) - 1, qRound(crop.shown.bottom() / ratio) - 1));
            if (shown != d->shown) {
                d->shown = shown;
                d->area->update();
            }
            d->pixels = pixels;
            d->dirty = false;
        }
        active = d->surface.show(d->area->window()->windowHandle(), d->shown.translated(d->area->mapTo(d->area->window(), QPoint())), d->packed);
    } else if (!visible) {
        d->surface.reset();
    } else {
        d->surface.reset();
    }
    // read after the surface was shown or let go: a surface that was reset (the picture hidden, another window) forgets its failure
    const QString failure = d->surface.failure();
    if (failure != d->failure) {
        d->failure = failure;
        if (d->area)
            d->area->setProperty("stereoLayerFailure", failure);
        Q_EMIT failureChanged();
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
    const QRect announced = shownRect();
    if (announced != d->announced) {
        d->announced = announced;
        if (d->area) {
            d->area->setProperty("stereoLayerShown", announced);
            d->area->update();
            d->area->window()->update();
        }
        Q_EMIT shownChanged();
    }
}
}
