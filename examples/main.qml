// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
import QtQuick
import QtQuick.Window
import org.sparky.stereo 1.0
Window {
    id: root
    required property url pictureSource
    width: 720
    height: 500
    flags: Qt.FramelessWindowHint
    color: "#eff0f1"
    visible: true
    Text { x: 20; y: 25; text: "QML controls stay 2D"; color: "black" }
    StereoPicture {
        id: photo
        x: 40; y: 90; width: 640; height: 360
        source: root.pictureSource
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_F1) { zoom = 1; pan = Qt.point(0, 0); }
            if (event.key === Qt.Key_F2) { zoom = 1.4; pan = Qt.point(20, -12); }
            if (event.key === Qt.Key_F3) visible = false;
            if (event.key === Qt.Key_F4) visible = true;
            if (event.key === Qt.Key_F5) view = 3;
            if (event.key === Qt.Key_F6) swapEyes = !swapEyes;
            if (event.key === Qt.Key_F8) view = 0;
            if (event.key === Qt.Key_F9) { x = 80; y = 110; width = 560; height = 320; }
            event.accepted = true;
        }
    }
    Text { x: 20; y: 470; text: "F2 zoom/pan   F3 hide   F5 2D   F6 swap   F9 move"; color: "black" }
}
