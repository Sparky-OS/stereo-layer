// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QObject>
#include <QPointF>
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
    void setPaintArea(bool enabled);
    // Keep the clear parts of the picture clear, showing what is behind the window there, instead of black. For an
    // adapter that gives each eye a whole view of its area, clear outside its stereo pictures; off by default.
    void setKeepTransparency(bool keep);
    bool isActive() const;
    bool approximateColor() const;
    void paint(QPainter *painter);
Q_SIGNALS:
    void activeChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
    void update();
};
}
