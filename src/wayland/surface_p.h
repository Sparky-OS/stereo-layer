// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QImage>
#include <QRect>
#include <QString>
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
    bool show(QWindow *parent, const QRect &rect, const QImage &packed);
    void hide();
    void reset();
    // Empty, or why the compositor did not take the surface as stereo content (the one message every player shows): it announces the stereo
    // global but the declaration could not be made or was refused. A compositor with no stereo global is not a failure (a 2D desktop). It stays
    // until reset(); show() returns false without trying again while it does.
    QString failure() const;

private:
    void fail(const QString &what);
    struct Private;
    std::unique_ptr<Private> d;
};
}
