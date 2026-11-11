// SPDX-License-Identifier: GPL-2.0-or-later
#include <picture.h>

int main()
{
    QImage packed(4, 1, QImage::Format_RGB32);
    packed.setPixelColor(0, 0, Qt::red);
    packed.setPixelColor(1, 0, Qt::red);
    packed.setPixelColor(2, 0, Qt::blue);
    packed.setPixelColor(3, 0, Qt::blue);
    auto picture = Stereo::Picture::fromPackedImage(packed, Stereo::Picture::Layout::SideBySide, Stereo::Picture::EyeOrder::LeftFirst);
    if (picture.left.size() != QSize(2, 1) || picture.right.size() != QSize(2, 1))
        return 1;
    if (picture.left.pixelColor(0, 0) != QColor(Qt::red) || picture.right.pixelColor(0, 0) != QColor(Qt::blue))
        return 2;
    return 0;
}
