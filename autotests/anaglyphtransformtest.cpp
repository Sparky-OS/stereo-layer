// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include <cstdio>
using Stereo::Picture;

// Independent integer oracle for the pre-existing reconstruction policy.
static QColor expected(int pair, int eye, int r, int g, int b)
{
    if (pair == 1)
        return eye ? QColor((r+g)/2,g,b) : QColor(r,(r+g)/2,(r+b)/2);
    if (pair == 2)
        return eye ? QColor(r,(r+g+b)/3,b) : QColor((r+g)/2,g,(g+b)/2);
    return eye ? QColor((r+b)/2,(g+b)/2,b) : QColor(r,g,(g+b)/2);
}
int main()
{
    QImage source(256, 32, QImage::Format_RGB32);
    for (int y=0; y<source.height(); ++y)
        for (int x=0; x<source.width(); ++x)
            source.setPixelColor(x,y,QColor(x,(x*37+y*17)%256,(x*71+y*13)%256));
    for (int pair=1; pair<=3; ++pair) {
        const auto kind = Picture::AnaglyphColorPair(pair);
        const auto result = Picture::fromAnaglyph(source,kind);
        if (!result.approximateColor || result.left.size()!=source.size() || result.right.size()!=source.size()) return 1;
        for (int eye=0; eye<2; ++eye) {
            int w[9],d[3];
            if (!Picture::anaglyphTransform(kind,!eye,w,d)) return 2;
            for (int y=0; y<source.height(); ++y) for (int x=0; x<source.width(); ++x) {
                const auto rgb=source.pixelColor(x,y);
                const auto want=expected(pair,eye,rgb.red(),rgb.green(),rgb.blue());
                if ((eye ? result.right : result.left).pixelColor(x,y)!=want) return 3;
                for (int c=0;c<3;++c) {
                    const int value=(w[3*c]*rgb.red()+w[3*c+1]*rgb.green()+w[3*c+2]*rgb.blue())/d[c];
                    if (value != (c==0 ? want.red() : c==1 ? want.green() : want.blue())) return 4;
                }
            }
        }
    }
    for (int bad : {0,-1,4,255}) {
        int w[9],d[3];
        if (Picture::anaglyphTransform(Picture::AnaglyphColorPair(bad),true,w,d)) return 5;
        for (int v:w) if(v) return 6;
        for (int v:d) if(v!=1) return 7;
        if (!Picture::fromAnaglyph(source,Picture::AnaglyphColorPair(bad)).left.isNull()) return 8;
    }
    std::puts("PASS 147456 exact channels; all colour pairs/eyes; invalid kinds; same-size approximate reconstruction");
}
