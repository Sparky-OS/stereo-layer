// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QObject>
#include <memory>
class QScreen;
namespace Stereo
{
class STEREO_LAYER_EXPORT ScreenMode : public QObject
{
    Q_OBJECT
public:
    explicit ScreenMode(QObject *parent = nullptr);
    ~ScreenMode() override;
    void setScreen(QScreen *screen);
    QScreen *screen() const;
    OutputMode mode() const;
Q_SIGNALS:
    void modeChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
    void update();
};
}
