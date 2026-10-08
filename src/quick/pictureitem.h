// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>
namespace Stereo
{
class STEREO_LAYER_EXPORT PictureItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StereoPicture)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool swapEyes READ swapEyes WRITE setSwapEyes NOTIFY optionsChanged)
    Q_PROPERTY(int inputLayout READ inputLayout WRITE setInputLayout NOTIFY optionsChanged)
    Q_PROPERTY(int inputOrder READ inputOrder WRITE setInputOrder NOTIFY optionsChanged)
    Q_PROPERTY(int anaglyph READ anaglyph WRITE setAnaglyph NOTIFY optionsChanged)
    Q_PROPERTY(int view READ view WRITE setView NOTIFY optionsChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY transformChanged)
    Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY transformChanged)
    Q_PROPERTY(bool stereoActive READ stereoActive NOTIFY stereoActiveChanged)
    Q_PROPERTY(bool approximateColor READ approximateColor NOTIFY pictureChanged)
    Q_PROPERTY(QString colorDisclaimer READ colorDisclaimer CONSTANT)
public:
    explicit PictureItem(QQuickItem* parent = nullptr);
    ~PictureItem() override;
    static bool prepareWindow(QQuickWindow* window);
    QUrl source() const;
    void setSource(const QUrl& source);
    bool swapEyes() const;
    void setSwapEyes(bool swap);
    int inputLayout() const;
    void setInputLayout(int layout);
    int inputOrder() const;
    void setInputOrder(int order);
    int anaglyph() const;
    void setAnaglyph(int pair);
    int view() const;
    void setView(int view);
    qreal zoom() const;
    void setZoom(qreal zoom);
    QPointF pan() const;
    void setPan(const QPointF& pan);
    bool stereoActive() const;
    bool approximateColor() const;
    QString colorDisclaimer() const;
Q_SIGNALS:
    void sourceChanged();
    void optionsChanged();
    void transformChanged();
    void pictureChanged();
    void stereoActiveChanged();

protected:
    void updatePolish() override;
    QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
    void reload();
    void syncWindow(QQuickWindow* window);
};
}
