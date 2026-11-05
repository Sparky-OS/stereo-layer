// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QWidget>
#include <stereo_layer_export.h>

namespace Stereo
{
class DepthSettings;

// The controls of the depth options (DepthSettings) for a Qt widgets program: the same set and words as the QML
// StereoDepthControls, so every player shows the same ones. The program shows them only for content that carries a depth map
// and keeps the settings per file in its own store.
class STEREO_LAYER_EXPORT DepthControls : public QWidget
{
    Q_OBJECT
public:
    explicit DepthControls(DepthSettings *settings, QWidget *parent = nullptr);
    ~DepthControls() override;

private:
    struct Private;
    Private *d;
};
}
