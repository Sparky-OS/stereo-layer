// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include "pictureitem.h"
#include "screenmode.h"
#include "widgetlayer.h"
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QShortcut>
#include <QTimer>
#include <QWindow>
using namespace Stereo;
class Demo : public QWidget
{
public:
    Demo(const QString &file, bool control = false, bool doubleArea = false)
    {
        setWindowTitle(QStringLiteral("Shared stereo picture"));
        setWindowFlag(Qt::FramelessWindowHint);
        resize(720, 500);
        if (!control)
            WidgetLayer::prepareWindow(this);
        auto *title = new QLabel(QStringLiteral("Picture controls stay 2D"), this);
        title->setGeometry(20, 15, 600, 40);
        area = control ? static_cast<QWidget *>(new QLabel(this)) : new QWidget(this);
        area->setGeometry(40, 90, 640, 360);
        picture = Picture::load(file, {}, OutputMode::Stereo);
        if (control) {
            auto *label = static_cast<QLabel *>(area);
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet(QStringLiteral("background-color: black;"));
            label->setPixmap(QPixmap::fromImage(picture.original.scaled(area->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        } else {
            layer = new WidgetLayer(area);
            layer->setSource(file);
            if (doubleArea) {
                area->setGeometry(40, 90, 300, 360);
                auto *secondArea = new QWidget(this);
                secondArea->setGeometry(380, 90, 300, 360);
                auto *secondLayer = new WidgetLayer(secondArea);
                secondLayer->setPicture(picture);
            }
        }
        auto *status = new QLabel(QStringLiteral("F2 zoom/pan   F3 hide   F4 show   F5 2D   F6 swap   F9 move"), this);
        status->setGeometry(20, 460, 690, 30);
    }
    QWidget *area = nullptr;
    WidgetLayer *layer = nullptr;
    Picture picture;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (!layer)
            return;
        switch (event->key()) {
        case Qt::Key_F1:
            layer->setTransform(1, {});
            break;
        case Qt::Key_F2:
            layer->setTransform(1.4, QPointF(20, -12));
            break;
        case Qt::Key_F3:
            area->hide();
            break;
        case Qt::Key_F4:
            area->show();
            break;
        case Qt::Key_F5: {
            Picture mono;
            mono.original = picture.left;
            layer->setPicture(mono);
            break;
        }
        case Qt::Key_F6:
            layer->setPicture(picture.swapped());
            break;
        case Qt::Key_F8:
            layer->setPicture(picture);
            break;
        case Qt::Key_F10:
            delete layer;
            layer = nullptr;
            area->hide();
            break;
        case Qt::Key_F9:
            area->setGeometry(80, 110, 560, 320);
            break;
        default:
            QWidget::keyPressEvent(event);
            break;
        }
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3)
        return 2;
    if (args.at(1) == QLatin1String("fixtures")) {
        const QString path = args.at(2);
        QImage left(200, 160, QImage::Format_RGB32), right(left.size(), left.format());
        for (auto pair : { std::pair<QImage *, QColor>(&left, Qt::red), std::pair<QImage *, QColor>(&right, Qt::blue) }) {
            pair.first->fill(pair.second);
            QPainter painter(pair.first);
            painter.setPen(Qt::white);
            painter.setFont(QFont(QStringLiteral("DejaVu Sans"), 24));
            painter.drawText(QRect(0, 5, 200, 40), Qt::AlignCenter, pair.first == &left ? QStringLiteral("LEFT") : QStringLiteral("RIGHT"));
            painter.fillRect(QRect(24, 70, 60, 12), Qt::white);
            painter.fillRect(QRect(110, 100, 20, 20), Qt::black);
        }
        QImage packed(400, 160, QImage::Format_RGB32);
        QPainter painter(&packed);
        painter.drawImage(0, 0, left);
        painter.drawImage(200, 0, right);
        painter.end();
        if (!packed.save(path + QStringLiteral("/pair.png")) || !left.save(path + QStringLiteral("/mono.png")))
            return 3;
        return 0;
    }
    if (args.at(1) == QLatin1String("qml")) {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({ { QStringLiteral("pictureSource"), QUrl::fromLocalFile(args.at(2)) } });
        engine.load(args.at(3));
        if (engine.rootObjects().isEmpty())
            return 4;
        return app.exec();
    }
    Demo demo(args.at(2), args.at(1) == QLatin1String("control"), args.at(1) == QLatin1String("double"));
    demo.show();
    return app.exec();
}
