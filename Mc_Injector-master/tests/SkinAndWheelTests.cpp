#include "../src/SkinCuboidGeometry.h"
#include "../src/SkinImage.h"
#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QDir>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    qmlRegisterType<SkinCuboidGeometry>("McOverlay",1,0,"SkinCuboidGeometry");
    int failed=0,checks=0;
    const auto check=[&](bool ok,const char* message) {
        ++checks; if(!ok) { ++failed; std::printf("FAIL %s\n",message); }
    };
    QImage legacy(64,32,QImage::Format_RGBA8888); legacy.fill(Qt::white);
    legacy.setPixelColor(4,20,Qt::red);
    const QImage normalized=skin::normalize(legacy);
    check(normalized.size()==QSize(64,64),"legacy atlas expanded without stretching");
    check(normalized.pixelColor(4,20)==QColor(Qt::red),"legacy base preserved");
    check(normalized.pixelColor(23,52)==QColor(Qt::red),"legacy left leg mirrored correctly");
    check(normalized.pixelColor(40,8).alpha()==0,"opaque legacy unused hat cleared");
    check(normalized.pixelColor(56,56).alpha()==0,"legacy does not invent clothing layers");
    check(skin::normalize(QImage(32,32,QImage::Format_RGBA8888)).isNull(),"reject malformed atlas");
    check(skin::normalize(normalized)==normalized,"modern skin roundtrip exact");
    struct Vertex { float x,y,z,nx,ny,nz,u,v; };
    for(int part=0;part<6;++part) for(bool slim:{false,true}) for(bool outer:{false,true}) {
        SkinCuboidGeometry geometry;
        geometry.setPart(static_cast<SkinCuboidGeometry::Part>(part));
        geometry.setSlim(slim); geometry.setOuterLayer(outer);
        Vertex vertices[24]; quint16 indices[36];
        check(geometry.vertexData().size()==sizeof(vertices),"geometry vertex count");
        std::memcpy(vertices,geometry.vertexData().constData(),sizeof(vertices));
        std::memcpy(indices,geometry.indexData().constData(),sizeof(indices));
        for(const auto& v:vertices)
            check(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1,"UV stays within atlas");
        for(int i=0;i<36;i+=3) {
            auto a=vertices[indices[i]],b=vertices[indices[i+1]],c=vertices[indices[i+2]];
            const QVector3D normal=QVector3D::crossProduct({b.x-a.x,b.y-a.y,b.z-a.z},
                                                         {c.x-a.x,c.y-a.y,c.z-a.z});
            check(QVector3D::dotProduct(normal,{a.nx,a.ny,a.nz})>0,"winding matches outward normal");
        }
        for(int face=0;face<6;++face)
            check(vertices[face*4].v<vertices[face*4+1].v,"UV image top faces upward on every side");
    }
    QTemporaryDir temp;
    QImage chart(64,64,QImage::Format_RGBA8888); chart.fill(Qt::transparent);
    QPainter p(&chart);
    p.fillRect(8,8,8,8,Qt::red); p.fillRect(20,20,8,12,Qt::green);
    p.fillRect(44,20,4,12,Qt::blue); p.fillRect(36,52,4,12,Qt::yellow);
    p.fillRect(4,20,4,12,Qt::cyan); p.fillRect(20,52,4,12,Qt::magenta); p.end();
    const QString atlas=temp.filePath("atlas.png"); chart.save(atlas);
    QQuickView view;
    view.setInitialProperties({{"skinUrl",QUrl::fromLocalFile(atlas)}});
    view.setSource(QUrl::fromLocalFile(QStringLiteral(MC_TEST_SOURCE_DIR "/SkinPreview.qml")));
    check(view.status()==QQuickView::Ready,"skin fixture QML loads");
    view.show(); view.hide(); view.show(); QTest::qWait(2000);
    const QImage image=view.grabWindow().convertToFormat(QImage::Format_RGB32);
    check(!image.isNull(),"real Quick3D frame captured");
    int red=0,green=0,blue=0,yellow=0,cyan=0,magenta=0;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        const QColor c=image.pixelColor(x,y);
        red += c.red()>100 && c.green()<30 && c.blue()<30;
        green += c.green()>100 && c.red()<30 && c.blue()<30;
        blue += c.blue()>100 && c.red()<30 && c.green()<30;
        yellow += c.red()>100 && c.green()>100 && c.blue()<30;
        cyan += c.blue()>100 && c.green()>100 && c.red()<30;
        magenta += c.red()>100 && c.blue()>100 && c.green()<30;
    }
    check(red>300 && green>300 && blue>300 && yellow>300 && cyan>300 && magenta>300,
          "all six body parts sample their correct front-face atlas regions");
    const QString capture=qEnvironmentVariable("MC_TEST_CAPTURE_DIR");
    if(!capture.isEmpty()) { QDir().mkpath(capture); image.save(capture+"/skin-uv-chart.png"); }
    const QString realSkin=qEnvironmentVariable("MC_TEST_SKIN_PATH");
    if(!realSkin.isEmpty()) {
        const auto realAtlas=skin::normalize(QImage(realSkin));
        check(!realAtlas.isNull(),"existing cached player skin is valid");
        const QString realPath=temp.filePath("real-atlas.png"); realAtlas.save(realPath);
        view.rootObject()->setProperty("skinUrl",QUrl::fromLocalFile(realPath));
        view.rootObject()->setProperty("modelYaw",25.0);
        QTest::qWait(600);
        if(!capture.isEmpty()) view.grabWindow().save(capture+"/skin-player-real.png");
    }
    view.hide();
    QQuickView wheel;
    wheel.setSource(QUrl::fromLocalFile(QStringLiteral(MC_TEST_SOURCE_DIR "/WheelPreview.qml")));
    check(wheel.status()==QQuickView::Ready,"wheel fixture QML loads");
    wheel.show(); QTest::qWait(250);
    QObject* page=wheel.rootObject()->findChild<QObject*>("wheelPage");
    const auto scroll=[&](QPoint pos) {
        QTest::mouseMove(&wheel,pos); QTest::qWait(50);
        QWheelEvent event(pos,wheel.mapToGlobal(pos),{},QPoint(0,-120),Qt::NoButton,
                          Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(&wheel,&event);
        // The controller intentionally eases wheel movement instead of
        // jumping immediately. Wait for the critically damped transition.
        QTest::qWait(360);
    };
    scroll({300,150});
    check(page && qAbs(page->property("contentY").toDouble()-128)<1,"wheel advances one high-sensitivity animated notch");
    for(int i=0;i<4;++i) {
        const QPointF local(300.0,150.0);
        QWheelEvent event(local,wheel.mapToGlobal(local),{},QPoint(0,-120),Qt::NoButton,
                          Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(&wheel,&event);
    }
    QTest::qWait(140);
    check(page && page->property("contentY").toDouble()>360,
          "rapid wheel packets accelerate one continuous motion");
    QTest::qWait(260);
    check(page && qAbs(page->property("contentY").toDouble()-640)<1,
          "rapid wheel packets retain their full accumulated distance");
    QTest::mousePress(&wheel,Qt::LeftButton,Qt::NoModifier,{300,160});
    for(int y=160;y>=40;y-=10) { QTest::mouseMove(&wheel,{300,y}); QTest::qWait(10); }
    QTest::mouseRelease(&wheel,Qt::LeftButton,Qt::NoModifier,{300,40});
    check(page && qAbs(page->property("contentY").toDouble()-640)<1,"mouse drag cannot scroll page");
    // Return the embedded zoom surface to the viewport before verifying that
    // its wheel handler wins over the surrounding page handler.
    if(page) page->setProperty("contentY",128.0);
    scroll({40,50});
    QObject* zone=wheel.rootObject()->findChild<QObject*>("skinZone");
    check(zone && zone->property("wheelCount").toInt()==1 &&
          qAbs(page->property("contentY").toDouble()-128)<1,"skin wheel zoom not stolen by page");
    std::printf("Skin/wheel: %d checks, %d failures; face pixels %d %d %d %d %d %d\n",
                checks,failed,red,green,blue,yellow,cyan,magenta);
    return failed ? 1 : 0;
}
