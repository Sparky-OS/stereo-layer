// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "pictureitem.h"
#include "render.h"
#include "screenmode.h"
#include "surface_p.h"
#include <QPainter>
#include <QPointer>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSurfaceFormat>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <cmath>
Q_DECLARE_LOGGING_CATEGORY(qLcLayer)
namespace Stereo
{
namespace
{
// Only a screen that shows two eyes takes the pair; any other shows the left view alone, as the file's flat picture.
bool showsTwoEyes(OutputMode mode) { return mode == OutputMode::Stereo || mode == OutputMode::Anaglyph; }
QString itemName(const QQuickItem *item)
{
    const QString type = QString::fromLatin1(item->metaObject()->className());
    return item->objectName().isEmpty() ? type : QStringLiteral("%1 \"%2\"").arg(type, item->objectName());
}
}
QString PictureItem::flatReason(const QQuickItem *item)
{
    const QRectF mapped = item->mapRectToScene(item->boundingRect());
    const qreal ratio = item->window() ? item->window()->devicePixelRatio() : 1;
    // less than half a device pixel of the item outside a clip shows no pixel the clip takes away
    const qreal slack = 0.5 / ratio;
    for (const QQuickItem *at = item; at; at = at->parentItem()) {
        if (!qFuzzyIsNull(at->rotation()))
            return QStringLiteral("%1 is rotated").arg(itemName(at));
        if (!qFuzzyCompare(at->scale(), qreal(1)))
            return QStringLiteral("%1 is scaled").arg(itemName(at));
        if (!qFuzzyCompare(at->opacity(), qreal(1)))
            return QStringLiteral("%1 is not opaque").arg(itemName(at));
        // the rectangle the scene graph clips to (QQuickWindow's clip node takes clipRect(), not the bounding rectangle)
        if (at->clip() && !at->mapRectToScene(at->clipRect()).adjusted(-slack, -slack, slack, slack).contains(mapped))
            return QStringLiteral("%1 clips it").arg(itemName(at));
    }
    return {};
}
struct PictureItem::Private {
    QUrl source;
    Picture::Options options;
    Picture picture;
    ScreenMode output;
    Surface surface;
    QImage packed;
    QRectF shown; // where the picture shows in the item, the rest of it black
    QRect hole; // the window's hole, in the window's own pixels: the rectangle the surface is laid over (Surface::show())
    QSize pixels;
    bool dirty = true;
    QPointer<QQuickWindow> window;
    QTimer timer;
    qreal zoom = 1;
    QPointF pan;
    bool active = false;
    QString failure;
    QSet<QString> told; // the reasons this item has already written to the log
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
void PictureItem::refresh() { reload(); }
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
    if (layout >= 0
        && (layout <= int(Picture::Layout::SideBySideQuincunx)
            || (layout >= int(Picture::Layout::DepthSideBySide) && layout <= int(Picture::Layout::DepthBackground)))
        && layout != inputLayout()) {
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
QString PictureItem::stereoFailure() const { return d->failure; }
bool PictureItem::isStereo() const { return d->picture.isStereo(); }
QSize PictureItem::pictureSize() const { return d->picture.isStereo() ? d->picture.left.size() : d->picture.original.size(); }
bool PictureItem::approximateColor() const { return d->picture.approximateColor; }
QString PictureItem::colorDisclaimer() const { return Picture::colorDisclaimer(); }
void PictureItem::updatePolish()
{
    bool active = false;
    const bool twoEyes = showsTwoEyes(d->output.mode());
    const QRectF mapped = mapRectToScene(boundingRect());
    const bool shown = window() && window()->isVisible() && isVisible() && twoEyes && d->picture.isStereo();
    // A native subsurface cannot follow the scene graph's rotation, scale, opacity or a clip that cuts the item; a clip that holds the
    // whole item (a Kirigami page, its ColumnView) takes nothing from it. Nor can it show through a window with no alpha channel (one
    // made before the picture asked for it, prepareWindow()). Where one of these holds, the item draws the left view in the scene graph,
    // where the clip and the rest apply as to any item, and the log says why once for each reason.
    QString reason = shown ? flatReason(this) : QString();
    if (shown && reason.isEmpty() && window()->format().alphaBufferSize() <= 0)
        reason = QStringLiteral("its window has no alpha channel (it was made before the picture asked for one)");
    if (!reason.isEmpty() && !d->told.contains(reason)) {
        d->told.insert(reason);
        qCWarning(qLcLayer).noquote() << QStringLiteral("StereoPicture %1: %2, so the picture is shown flat, one eye, not in 3D, while it is")
                                             .arg(d->source.toDisplayString(), reason);
    }
    if (shown && reason.isEmpty()) {
        const qreal ratio = window()->devicePixelRatio();
        const QSize pixels(qRound(mapped.width() * ratio), qRound(mapped.height() * ratio));
        if (d->dirty || d->pixels != pixels) {
            // each eye's samples that show, at the resolution they have; the compositor resizes them once
            const EyeCrop crop = cropEyes(d->picture, pixels, d->zoom, d->pan * ratio);
            d->packed = crop.packed;
            if (d->packed.hasAlphaChannel()) {
                // the clear parts of the picture black, as around it
                QImage opaque(d->packed.size(), QImage::Format_RGB32);
                opaque.fill(Qt::black);
                QPainter painter(&opaque);
                painter.drawImage(QPoint(0, 0), d->packed);
                painter.end();
                d->packed = opaque;
            }
            const QRectF shown(QPointF(std::round(crop.shown.left() / ratio), std::round(crop.shown.top() / ratio)),
                QPointF(std::round(crop.shown.right() / ratio), std::round(crop.shown.bottom() / ratio)));
            if (shown != d->shown) {
                d->shown = shown;
                update();
            }
            d->pixels = pixels;
            d->dirty = false;
        }
        // the picture's own size at the nearest whole pixel of the window: the compositor shows the eyes in it with no
        // stretch (a rectangle rounded outward was up to a pixel larger on each side than the picture it holds)
        const QRectF placed = d->shown.translated(mapped.topLeft());
        const QRect hole(QPoint(qRound(placed.left()), qRound(placed.top())), QSize(qRound(placed.width()), qRound(placed.height())));
        if (hole != d->hole) {
            d->hole = hole;
            update();
        }
        active = d->surface.show(window(), hole, d->packed);
    } else {
        d->surface.reset();
    }
    const QString failure = d->surface.failure();
    if (failure != d->failure) {
        d->failure = failure;
        Q_EMIT stereoFailureChanged();
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
        // the picture's surface lies below the window: black around it, a hole where it shows
        auto *background = new QSGSimpleRectNode(boundingRect(), Qt::black);
        // The hole lies on whole device pixels, the ones Surface::covering() counts for the rectangle the surface is laid
        // over (each edge at the nearest whole device pixel). The item itself may lie on parts of a pixel at a scale that
        // is not whole (125 %), and the scene graph's rasteriser takes a pixel whose centre is on a rectangle's left or
        // top edge: a hole drawn at the item's own place could start a device pixel before the surface, and the desktop
        // showed through that column (Koko at 125 %, ORDER 551c).
        const qreal ratio = window()->devicePixelRatio();
        const QPointF origin = mapToScene(QPointF(0, 0));
        const auto device = [ratio](int logical) { return qRound(logical * ratio) / ratio; };
        const QRectF holeRect(QPointF(device(d->hole.left()), device(d->hole.top())) - origin,
            QPointF(device(d->hole.left() + d->hole.width()), device(d->hole.top() + d->hole.height())) - origin);
        auto *hole = new QSGSimpleRectNode(holeRect.intersected(boundingRect()), Qt::transparent);
        hole->material()->setFlag(QSGMaterial::Blending, false);
        background->appendChildNode(hole);
        return background;
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
