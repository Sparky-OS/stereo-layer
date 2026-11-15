// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QObject>
#include <QPointF>
#include <QRect>
#include <memory>
class QWidget;
class QPainter;
namespace Stereo
{
class STEREO_LAYER_EXPORT WidgetLayer : public QObject
{
    Q_OBJECT
public:
    explicit WidgetLayer(QWidget *area, QObject *parent = nullptr);
    ~WidgetLayer() override;
    static bool prepareWindow(QWidget *window);
    static bool isSupported();
    void setPicture(const Picture &picture);
    void setSource(const QString &path, const Picture::Options &options = {});
    void setTransform(qreal zoom, const QPointF &pan);
    // For an adapter that draws its area once for each eye (a view of many items, a preview with its frame): the eyes on the
    // window's own device pixels, so that at any scale, whole or not, each eye holds the very pixels the window paints in the
    // area, and a hole the window leaves for a picture lies over that picture to the pixel.
    // eyeCanvas() gives a clear image from the corner of the window's content to the far corner of the area, with the
    // window's device pixel ratio; the adapter draws the area into it at eyeOffset(), as the window itself paints it there:
    //     QImage eye = layer->eyeCanvas();
    //     area->render(&eye, layer->eyeOffset(), QRegion(), QWidget::DrawChildren);   // or a QPainter translated by eyeOffset()
    // and hands both to setEyes(). The layer takes the area's own pixels out of each and shows them one to one: no resize
    // anywhere. The eyes are good for the place, size and scale the area has when setEyes() is called; when any of them
    // changes the layer goes flat at once (isActive() false, activeChanged()) and asks for new eyes with eyesWanted().
    // Such an adapter paints its area itself (setPaintArea(false)): flat while the layer is not active, with its holes while it is.
    QImage eyeCanvas() const;
    QPoint eyeOffset() const;
    void setEyes(const QImage &left, const QImage &right);
    void setPaintArea(bool enabled);
    // Keep the clear parts of the picture clear, showing what is behind the window there, instead of black. For an
    // adapter that gives each eye a whole view of its area, clear outside its stereo pictures; off by default.
    void setKeepTransparency(bool keep);
    bool isActive() const;
    // Where the picture shows in the area, in the area's coordinates; empty while the layer is not active. The picture's
    // surface lies below the window and this rectangle is the window's hole: what a viewer paints in the area around it
    // is its own and opaque, never see-through. The area carries the same rectangle as its property "stereoLayerShown".
    QRect shownRect() const;
    // Empty, or why the compositor announces stereo and the picture is still shown flat: the declaration of its surface failed (also logged once,
    // category org.sparky.stereo.layer). The one message every player shows; a compositor with no stereo is no failure.
    QString failure() const;
    bool approximateColor() const;
    void paint(QPainter *painter);
Q_SIGNALS:
    void activeChanged();
    void failureChanged();
    void shownChanged();
    // The eyes given with setEyes() no longer fit the area (it moved, changed size or scale): draw them again
    void eyesWanted();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
    void update();
    bool eyesFit() const;
    void dropStaleEyes();
};
}
