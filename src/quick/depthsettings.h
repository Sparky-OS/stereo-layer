// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace Stereo
{
// The options of drawing the views of a picture with its depth map, as one set for every player: the values of libstereo-dibr's
// stereo_dibr_options (the best default measured on scenes with known views, and the person's choice), for the controls to
// show (DepthControls, StereoDepthControls in QML) and for the program to give to the drawing. The person's choice is kept by
// the program in its own store, as text(): the library's text of the set (stereo_dibr_options_write).
class STEREO_LAYER_EXPORT DepthSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StereoDepthSettings)
    // enum stereo_dibr_view, _warp, _fill, _smooth
    Q_PROPERTY(int view READ view WRITE setView NOTIFY changed)
    Q_PROPERTY(int warp READ warp WRITE setWarp NOTIFY changed)
    Q_PROPERTY(int fill READ fill WRITE setFill NOTIFY changed)
    Q_PROPERTY(int smooth READ smooth WRITE setSmooth NOTIFY changed)
    Q_PROPERTY(double sigmaH READ sigmaH WRITE setSigmaH NOTIFY changed)
    Q_PROPERTY(double sigmaV READ sigmaV WRITE setSigmaV NOTIFY changed)
    Q_PROPERTY(int radius READ radius WRITE setRadius NOTIFY changed)
    // pixels of a picture 1920 wide
    Q_PROPERTY(double strength READ strength WRITE setStrength NOTIFY changed)
    Q_PROPERTY(double zero READ zero WRITE setZero NOTIFY changed)
    Q_PROPERTY(double shift READ shift WRITE setShift NOTIFY changed)
    // for depth that holds distances; 0 takes what the file says
    Q_PROPERTY(double nearDistance READ nearDistance WRITE setNearDistance NOTIFY changed)
    Q_PROPERTY(double farDistance READ farDistance WRITE setFarDistance NOTIFY changed)
    // the set as text, to keep and to give back; empty for the default
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
    Q_PROPERTY(bool isDefault READ isDefault NOTIFY changed)
    // The way of drawing is not the person's: a still picture and the saving into a file use the best way, a video the best way
    // that keeps its frame rate on the GPU that draws it (the player gives the choice with setChoice()). Choosing a warping or a
    // gap filling turns it off, and the choice then holds for everything.
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
    // The warping and the gap filling the controls show: what the video is drawn with while the way is automatic and the player has
    // told the choice, else warp and fill
    Q_PROPERTY(int shownWarp READ shownWarp NOTIFY changed)
    Q_PROPERTY(int shownFill READ shownFill NOTIFY changed)
    // What was chosen and why, as a sentence in the person's language ("Layered, Pyramid (Layered, Depth-aided does not keep 30
    // frames a second on this GPU)"); empty when the way is the person's or no video is drawing
    Q_PROPERTY(QString chosenText READ chosenText NOTIFY changed)
    Q_PROPERTY(double strengthMax READ strengthMax CONSTANT)
    Q_PROPERTY(double shiftMax READ shiftMax CONSTANT)
    Q_PROPERTY(int radiusMax READ radiusMax CONSTANT)

public:
    explicit DepthSettings(QObject *parent = nullptr);
    ~DepthSettings() override;

    int view() const;
    void setView(int value);
    int warp() const;
    void setWarp(int value);
    int fill() const;
    void setFill(int value);
    int smooth() const;
    void setSmooth(int value);
    double sigmaH() const;
    void setSigmaH(double value);
    double sigmaV() const;
    void setSigmaV(double value);
    int radius() const;
    void setRadius(int value);
    double strength() const;
    void setStrength(double value);
    double zero() const;
    void setZero(double value);
    double shift() const;
    void setShift(double value);
    double nearDistance() const;
    void setNearDistance(double value);
    double farDistance() const;
    void setFarDistance(double value);

    QString text() const;
    // Takes the library's text of a set; an empty text is the default; a text that does not read is refused (false) and the
    // set stays as it was
    bool setText(const QString &text);
    bool isDefault() const;
    static double strengthMax();
    static double shiftMax();
    static int radiusMax();

    bool automatic() const;
    void setAutomatic(bool value);
    int shownWarp() const;
    int shownFill() const;
    QString chosenText() const;
    // The person picks a way: both the warping and the gap filling are theirs from now on, even when they are the ones shown
    Q_INVOKABLE void useWay(int warp, int fill);
    // What the player's drawing chose for the video: the map of QQuickVideoOutput::stereoDepthChoice (warp, fill, keepsUp, fps,
    // skippedWarp, skippedFill, onGpu); an empty map says no video is drawing
    Q_INVOKABLE void setChoice(const QVariantMap &choice);
    Q_INVOKABLE void reset();
    // The options as the picture drawing takes them
    Picture::DepthOptions toOptions() const;

    // The words of the controls, translated, one source for the widgets and the QML: key is one of strength, zero, shift,
    // picture, pictureLeft, pictureMiddle, fill, fillBackground, fillDepthAided, fillTelea, fillNone, fillPyramid, automatic,
    // automaticTip, warp, warpForward,
    // warpBackward, warpLayered, smooth, reach, nearest, farthest, reset, title and the tip of strength, zero, picture, fill
    // (strengthTip...)
    Q_INVOKABLE static QString label(const QString &key);

Q_SIGNALS:
    void changed();

private:
    struct Private;
    Private *d;
};
}
