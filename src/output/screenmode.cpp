// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "screenmode.h"
#include <KScreen/Config>
#include <KScreen/ConfigMonitor>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <QPointer>
#include <QScreen>

namespace Stereo
{
struct ScreenMode::Private {
    QPointer<QScreen> screen;
    KScreen::ConfigPtr config;
    OutputMode mode = OutputMode::Unknown;
};
ScreenMode::ScreenMode(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged, this, &ScreenMode::update);
    auto* operation = new KScreen::GetConfigOperation(KScreen::ConfigOperation::NoEDID, this);
    connect(operation, &KScreen::ConfigOperation::finished, this, [this, operation] {
        if (!operation->hasError()) {
            d->config = operation->config();
            KScreen::ConfigMonitor::instance()->addConfig(d->config);
            update();
        }
    });
}
ScreenMode::~ScreenMode()
{
    if (d->config)
        KScreen::ConfigMonitor::instance()->removeConfig(d->config);
}
void ScreenMode::setScreen(QScreen* screen)
{
    if (d->screen == screen)
        return;
    if (d->screen)
        disconnect(d->screen, nullptr, this, nullptr);
    d->screen = screen;
    if (screen)
        connect(screen, &QObject::destroyed, this, &ScreenMode::update);
    update();
}
QScreen* ScreenMode::screen() const { return d->screen; }
OutputMode ScreenMode::mode() const { return d->mode; }
void ScreenMode::update()
{
    OutputMode mode = OutputMode::Unknown;
    if (d->screen && d->config) {
        for (const auto& output : d->config->outputs()) {
            if (output->name() != d->screen->name() || !output->isEnabled() || !output->isConnected())
                continue;
            const auto active = output->currentMode();
            if (!active)
                break;
            switch (active->stereo3D()) {
            case KScreen::Mode::Stereo3D::None:
                mode = OutputMode::Mono;
                break;
            case KScreen::Mode::Stereo3D::AnaglyphModern:
            case KScreen::Mode::Stereo3D::AnaglyphCrt:
                mode = OutputMode::Anaglyph;
                break;
            default:
                mode = OutputMode::Stereo;
                break;
            }
            break;
        }
    }
    if (mode != d->mode) {
        d->mode = mode;
        Q_EMIT modeChanged();
    }
}
}
