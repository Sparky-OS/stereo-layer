// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QImage>
#include <QRect>
#include <memory>
class QWindow;
namespace Stereo
{
class Surface
{
public:
    Surface();
    ~Surface();
    static bool isSupported();
    bool show(QWindow* parent, const QRect& rect, const QImage& packed);
    void hide();
    void reset();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
