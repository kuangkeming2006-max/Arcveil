#pragma once
#include <QImage>
#include <QPainter>

namespace skin {
// Normalize legacy skins once, before handing the atlas to the GPU. Never
// stretch 64x32: that changes every UV and loses the mirrored left limbs.
inline QImage normalize(const QImage& input) {
    if (input.width()!=64 || (input.height()!=32 && input.height()!=64)) return {};
    QImage out(64,64,QImage::Format_RGBA8888);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0,0,input);
    if (input.height()==32) {
        const auto mirror=[&](int sx,int sy,int w,int h,int dx,int dy) {
            painter.drawImage(dx,dy,input.copy(sx,sy,w,h).flipped(Qt::Horizontal));
        };
        for (int arm=0; arm<2; ++arm) {
            const int src=arm ? 40 : 0, dst=arm ? 32 : 16;
            mirror(src+4,16,4,4,dst+4,48);
            mirror(src+8,16,4,4,dst+8,48);
            mirror(src+8,20,4,12,dst,52);
            mirror(src,20,4,12,dst+8,52);
            mirror(src+4,20,4,12,dst+4,52);
            mirror(src+12,20,4,12,dst+12,52);
        }
    }
    painter.end();
    // Original legacy skins often have an opaque unused hat rectangle. Vanilla
    // interprets a completely opaque legacy hat area as absent.
    if (input.height()==32) {
        bool opaque=true;
        for(int y=0;y<16;++y) for(int x=32;x<64;++x)
            opaque = opaque && qAlpha(input.pixel(x,y))==255;
        if(opaque) for(int y=0;y<16;++y) for(int x=32;x<64;++x)
            out.setPixelColor(x,y,Qt::transparent);
    }
    return out;
}
}
