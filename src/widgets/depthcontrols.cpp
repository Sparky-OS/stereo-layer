// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "depthcontrols.h"
#include "depthsettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <stereo-dibr.h>

namespace Stereo
{
struct DepthControls::Private {
    DepthSettings *settings;
    QDoubleSpinBox *strength, *zero, *shift, *nearest, *farthest;
    QComboBox *picture, *fill, *warp;
    QCheckBox *smooth;
    QSpinBox *reach;
    QPushButton *reset;
};

DepthControls::DepthControls(DepthSettings *settings, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->settings = settings;
    auto tr = [](const char *key) { return DepthSettings::label(QLatin1String(key)); };
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("title"), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    outer->addWidget(title);
    auto *form = new QFormLayout;
    outer->addLayout(form);

    auto spin = [&](double max, double min, int decimals, double step, const QString &tip = QString()) {
        auto *box = new QDoubleSpinBox(this);
        box->setRange(min, max);
        box->setDecimals(decimals);
        box->setSingleStep(step);
        box->setToolTip(tip);
        return box;
    };
    d->strength = spin(DepthSettings::strengthMax(), 0, 0, 1, tr("strengthTip"));
    d->strength->setSuffix(QStringLiteral(" px"));
    d->zero = spin(1, 0, 2, 0.05, tr("zeroTip"));
    d->shift = spin(DepthSettings::shiftMax(), -DepthSettings::shiftMax(), 0, 1);
    d->shift->setSuffix(QStringLiteral(" px"));
    d->picture = new QComboBox(this);
    d->picture->addItem(tr("pictureLeft"), STEREO_DIBR_VIEW_LEFT);
    d->picture->addItem(tr("pictureMiddle"), STEREO_DIBR_VIEW_CENTER);
    d->picture->setToolTip(tr("pictureTip"));
    d->fill = new QComboBox(this);
    d->fill->addItem(tr("fillBackground"), STEREO_DIBR_FILL_BACKGROUND);
    d->fill->addItem(tr("fillDepthAided"), STEREO_DIBR_FILL_DEPTH_AIDED);
    d->fill->addItem(tr("fillTelea"), STEREO_DIBR_FILL_TELEA);
    d->fill->addItem(tr("fillNone"), STEREO_DIBR_FILL_NONE);
    d->fill->setToolTip(tr("fillTip"));
    d->warp = new QComboBox(this);
    d->warp->addItem(tr("warpForward"), STEREO_DIBR_WARP_FORWARD);
    d->warp->addItem(tr("warpBackward"), STEREO_DIBR_WARP_BACKWARD);
    d->warp->addItem(tr("warpLayered"), STEREO_DIBR_WARP_LAYERED);
    d->smooth = new QCheckBox(tr("smooth"), this);
    d->reach = new QSpinBox(this);
    d->reach->setRange(1, DepthSettings::radiusMax());
    d->nearest = spin(1e9, 0, 2, 1);
    d->farthest = spin(1e9, 0, 2, 1);
    d->reset = new QPushButton(tr("reset"), this);

    form->addRow(tr("strength"), d->strength);
    form->addRow(tr("zero"), d->zero);
    form->addRow(tr("shift"), d->shift);
    form->addRow(tr("picture"), d->picture);
    form->addRow(tr("fill"), d->fill);
    form->addRow(tr("warp"), d->warp);
    form->addRow(QString(), d->smooth);
    form->addRow(tr("reach"), d->reach);
    form->addRow(tr("nearest"), d->nearest);
    form->addRow(tr("farthest"), d->farthest);
    outer->addWidget(d->reset, 0, Qt::AlignLeft);

    // the controls show the settings; a change of a control is a change of the settings, and the other way round
    auto show = [this] {
        const QSignalBlocker b1(d->strength), b2(d->zero), b3(d->shift), b4(d->picture), b5(d->fill), b6(d->warp), b7(d->smooth), b8(d->reach), b9(d->nearest),
            b10(d->farthest);
        d->strength->setValue(d->settings->strength());
        d->zero->setValue(d->settings->zero());
        d->shift->setValue(d->settings->shift());
        d->picture->setCurrentIndex(d->picture->findData(d->settings->view()));
        d->fill->setCurrentIndex(d->fill->findData(d->settings->fill()));
        d->warp->setCurrentIndex(d->warp->findData(d->settings->warp()));
        d->smooth->setChecked(d->settings->smooth() == STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN);
        d->reach->setValue(d->settings->radius());
        d->nearest->setValue(d->settings->nearDistance());
        d->farthest->setValue(d->settings->farDistance());
        d->reset->setEnabled(!d->settings->isDefault());
    };
    show();
    connect(settings, &DepthSettings::changed, this, show);
    connect(d->strength, &QDoubleSpinBox::valueChanged, settings, &DepthSettings::setStrength);
    connect(d->zero, &QDoubleSpinBox::valueChanged, settings, &DepthSettings::setZero);
    connect(d->shift, &QDoubleSpinBox::valueChanged, settings, &DepthSettings::setShift);
    connect(d->picture, &QComboBox::activated, settings, [this] { d->settings->setView(d->picture->currentData().toInt()); });
    connect(d->fill, &QComboBox::activated, settings, [this] { d->settings->setFill(d->fill->currentData().toInt()); });
    connect(d->warp, &QComboBox::activated, settings, [this] { d->settings->setWarp(d->warp->currentData().toInt()); });
    connect(d->smooth, &QCheckBox::toggled, settings, [this](bool on) { d->settings->setSmooth(on ? STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN : STEREO_DIBR_SMOOTH_OFF); });
    connect(d->reach, &QSpinBox::valueChanged, settings, &DepthSettings::setRadius);
    connect(d->nearest, &QDoubleSpinBox::valueChanged, settings, &DepthSettings::setNearDistance);
    connect(d->farthest, &QDoubleSpinBox::valueChanged, settings, &DepthSettings::setFarDistance);
    connect(d->reset, &QPushButton::clicked, settings, &DepthSettings::reset);
}

DepthControls::~DepthControls()
{
    delete d;
}
}
