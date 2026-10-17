// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "pictureitem.h"
#include "render.h"
#include "screenmode.h"
#include "surface_p.h"
#include <QPointer>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSurfaceFormat>
#include <QTimer>
#include <cmath>
namespace Stereo
{
namespace
{
// Only a screen that shows two eyes takes the pair; any other shows the left view alone, as the file's flat picture.
bool showsTwoEyes(OutputMode mode) { return mode == OutputMode::Stereo || mode == OutputMode::Anaglyph; }
}
struct PictureItem::Private {
    QUrl source;
    Picture::Options options;
    Picture picture;
    ScreenMode output;
    Surface surface;
    QImage packed;
    QSize pixels;
    bool dirty = true;
    QPointer<QQuickWindow> window;
    QTimer timer;
    qreal zoom = 1;
    QPointF pan;
    bool active = false;
};
PictureItem::PictureItem(QQuickItem *parent)
    : QQuickItem(parent)
    , d(std::make_unique<Private>())
{
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::windowChanged, this, &PictureItem::syncWindow);
    connect(&d->output, &ScreenMode::modeChanged, this, &PictureItem::reload);
    d->timer.setInterval(50);
    connect(&d->timer, &QTimer::timeout, this, [this] { polish(); });
    d->timer.start();
    if (window())
        syncWindow(window());
}
PictureItem::~PictureItem() = default;
bool PictureItem::prepareWindow(QQuickWindow *window)
{
    if (window->handle())
        return window->format().alphaBufferSize() > 0;
    auto format = window->format();
    format.setAlphaBufferSize(qMax(8, format.alphaBufferSize()));
    window->setFormat(format);
    return true;
}
void PictureItem::syncWindow(QQuickWindow *window)
{
    d->surface.reset();
    if (d->window)
        disconnect(d->window, nullptr, &d->output, nullptr);
    d->window = window;
    d->output.setScreen(window ? window->screen() : nullptr);
    if (window) {
        prepareWindow(window);
        connect(window, &QWindow::screenChanged, &d->output, &ScreenMode::setScreen, Qt::UniqueConnection);
    }
    polish();
}
QUrl PictureItem::source() const { return d->source; }
void PictureItem::setSource(const QUrl &source)
{
    if (d->source == source)
        return;
    d->source = source;
    reload();
    Q_EMIT sourceChanged();
}
void PictureItem::reload()
{
    const QUrl resolved = qmlContext(this) ? qmlContext(this)->resolvedUrl(d->source) : d->source;
    const QString path = resolved.isLocalFile()     ? resolved.toLocalFile()
        : resolved.scheme() == QLatin1String("qrc") ? QLatin1String(":") + resolved.path()
                                                    : QString();
    d->dirty = true;
    d->picture = path.isEmpty() ? Picture {} : Picture::load(path, d->options, d->output.mode());
    polish();
    update();
    Q_EMIT pictureChanged();
}
bool PictureItem::swapEyes() const { return d->options.swapEyes; }
void PictureItem::setSwapEyes(bool swap)
{
    if (swap != d->options.swapEyes) {
        d->options.swapEyes = swap;
        reload();
        Q_EMIT optionsChanged();
    }
}
int PictureItem::inputLayout() const { return int(d->options.inputLayout); }
void PictureItem::setInputLayout(int layout)
{
    if (layout >= 0 && layout <= int(Picture::Layout::SideBySideQuincunx) && layout != inputLayout()) {
        d->options.inputLayout = Picture::Layout(layout);
        reload();
        Q_EMIT optionsChanged();
    }
}
int PictureItem::inputOrder() const { return int(d->options.inputOrder); }
void PictureItem::setInputOrder(int order)
{
    if (order >= 0 && order <= 1 && order != inputOrder()) {
        d->options.inputOrder = Picture::EyeOrder(order);
        reload();
        Q_EMIT optionsChanged();
    }
}
int PictureItem::anaglyph() const { return int(d->options.anaglyph); }
void PictureItem::setAnaglyph(int pair)
{
    if (pair >= 0 && pair <= int(Picture::AnaglyphColorPair::AmberBlue) && pair != anaglyph()) {
        d->options.anaglyph = Picture::AnaglyphColorPair(pair);
        reload();
        Q_EMIT optionsChanged();
    }
}
int PictureItem::view() const { return int(d->options.view); }
void PictureItem::setView(int view)
{
    if (view >= 0 && view <= int(Picture::View::Mono) && view != this->view()) {
        d->options.view = Picture::View(view);
        reload();
        Q_EMIT optionsChanged();
    }
}
qreal PictureItem::zoom() const { return d->zoom; }
void PictureItem::setZoom(qreal zoom)
{
    if (std::isfinite(zoom) && zoom > 0 && zoom != d->zoom) {
        d->zoom = zoom;
        d->dirty = true;
        polish();
        update();
        Q_EMIT transformChanged();
    }
}
QPointF PictureItem::pan() const { return d->pan; }
void PictureItem::setPan(const QPointF &pan)
{
    if (std::isfinite(pan.x()) && std::isfinite(pan.y()) && pan != d->pan) {
        d->pan = pan;
        d->dirty = true;
        polish();
        update();
        Q_EMIT transformChanged();
    }
}
bool PictureItem::stereoActive() const { return d->active; }
bool PictureItem::isStereo() const { return d->picture.isStereo(); }
QSize PictureItem::pictureSize() const { return d->picture.isStereo() ? d->picture.left.size() : d->picture.original.size(); }
bool PictureItem::approximateColor() const { return d->picture.approximateColor; }
QString PictureItem::colorDisclaimer() const { return Picture::colorDisclaimer(); }
void PictureItem::updatePolish()
{
    bool active = false;
    const bool twoEyes = showsTwoEyes(d->output.mode());
    const QRectF mapped = mapRectToScene(boundingRect());
    // Native subsurfaces cannot follow scene-graph rotation, opacity or clipping.
    bool aligned = qFuzzyIsNull(rotation()) && qFuzzyCompare(scale(), qreal(1)) && qFuzzyCompare(opacity(), qreal(1)) && !clip();
    for (QQuickItem *ancestor = parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        aligned &= qFuzzyIsNull(ancestor->rotation()) && qFuzzyCompare(ancestor->scale(), qreal(1)) && qFuzzyCompare(ancestor->opacity(), qreal(1))
            && !ancestor->clip();
    }
    if (window() && window()->isVisible() && isVisible() && aligned && twoEyes && d->picture.isStereo() && window()->format().alphaBufferSize() > 0) {
        const qreal ratio = window()->devicePixelRatio();
        const QSize pixels(qRound(mapped.width() * ratio), qRound(mapped.height() * ratio));
        if (d->dirty || d->pixels != pixels) {
            d->packed = renderEyes(d->picture, pixels, d->zoom, d->pan * ratio);
            d->pixels = pixels;
            d->dirty = false;
        }
        active = d->surface.show(window(), mapped.toAlignedRect(), d->packed);
    } else {
        d->surface.reset();
    }
    if (active != d->active) {
        d->active = active;
        update();
        Q_EMIT stereoActiveChanged();
    }
}
QSGNode *PictureItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    delete node;
    if (!window() || boundingRect().isEmpty())
        return nullptr;
    if (d->active) {
        auto *hole = new QSGSimpleRectNode(boundingRect(), Qt::transparent);
        hole->material()->setFlag(QSGMaterial::Blending, false);
        return hole;
    }
    // Drawn flat (a screen with one eye, or no surface of its own yet), a pair shows its left view, as the file's flat picture.
    const QImage &flat = d->picture.isStereo() ? d->picture.left : d->picture.original;
    auto *texture = window()->createTextureFromImage(renderMono(flat, QSize(qMax(1, qRound(width())), qMax(1, qRound(height()))), d->zoom, d->pan));
    if (!texture)
        return nullptr;
    auto *image = new QSGSimpleTextureNode;
    image->setTexture(texture);
    image->setOwnsTexture(true);
    image->setRect(boundingRect());
    return image;
}
}
