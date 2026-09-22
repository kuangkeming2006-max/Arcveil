#pragma once
#include <QObject>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QTest>
#include <QDir>
#include <QImage>
#include <functional>

// Explicit --smoke-test only. Delivers events to this owned QQuickWindow;
// never moves the system pointer or sends input to another application.
class ControllerUiSmoke final : public QObject {
public:
    ControllerUiSmoke(QQuickWindow* window, std::function<void(bool)> finished,
        std::function<void(bool)> setTheme = {})
        : QObject(window), m_window(window), m_finished(std::move(finished)),
          m_setTheme(std::move(setTheme)) {
        connect(&m_timer,&QTimer::timeout,this,[this] { step(); });
        m_timer.start(500);
    }
private:
    static QQuickItem* find(QQuickItem* parent,const QString& name) {
        if(parent->objectName()==name) return parent;
        for(auto* child:parent->childItems()) if(auto* match=find(child,name)) return match;
        return nullptr;
    }
    bool click(const QString& route) {
        auto* item=find(m_window->contentItem(),"navigation_"+route);
        if(!item || !item->isVisible() || !item->isEnabled()) return false;
        auto position=item->mapToScene(QPointF(item->width()/2,item->height()/2));
        qInfo() << "UI click" << route << position << "item" << item->size()
                << "window" << m_window->isVisible() << m_window->isExposed();
        // QTest's QWindow overload goes through Qt's window-system input path
        // (button state, timestamps and delivery), unlike sending a raw event.
        // It does not move the system cursor or address any external window.
        QTest::mouseClick(m_window,Qt::LeftButton,Qt::NoModifier,position.toPoint());
        qInfo() << "Route after click:" << m_window->property("activeRoute");
        return true;
    }
    void finish(bool ok) {
        m_timer.stop();
        qInfo() << "Controller UI clicks/modal transitions:" << (ok ? "passed" : "FAILED")
                << "phase" << m_phase;
        m_finished(ok);
        deleteLater();
    }
    void step() {
        const int phase=m_phase++;
        if(phase==0) { if(!click("settings")) finish(false); }
        else if(phase==1) {
            if(m_window->property("activeRoute").toString()!="settings") { finish(false); return; }
            m_popup=m_window->findChild<QObject*>("attachDialog");
            qInfo() << "Found attach popup:" << m_popup;
            if(!m_popup || !QMetaObject::invokeMethod(m_popup,"open")) finish(false);
        } else if(phase==2) {
            if(!m_popup->property("opened").toBool()) {
                qInfo()<<"Waiting for modal enter:"<<m_popup->property("visible")
                       <<m_popup->property("opacity")<<m_popup->property("scale");
                if(++m_enterWaits<5) {--m_phase;return;}
                finish(false); return;
            }
            click("scanner");
            if(m_window->property("activeRoute").toString()!="settings") { finish(false); return; }
            QMetaObject::invokeMethod(m_popup,"close");
        } else if(phase==3) {
            if(m_popup->property("visible").toBool() || !click("scanner")) finish(false);
        } else if(phase==4) {
            if(m_window->property("activeRoute").toString()!="scanner") { finish(false); return; }
            // Interrupt enter transitions: no invisible modal blocker may remain.
            QMetaObject::invokeMethod(m_popup,"open");
            QMetaObject::invokeMethod(m_popup,"close");
            QMetaObject::invokeMethod(m_popup,"open");
            QMetaObject::invokeMethod(m_popup,"close");
        } else if(phase==5) {
            if(m_popup->property("visible").toBool() || !click("settings")) finish(false);
        } else if(phase==6) {
            if(m_window->property("activeRoute").toString()!="settings" || !click("scanner")) finish(false);
        } else if(phase==7) {
            auto* refresh=find(m_window->contentItem(),"scanRefreshButton");
            if(!refresh || !refresh->isEnabled()) { finish(false); return; }
            auto position=refresh->mapToScene(QPointF(refresh->width()/2,refresh->height()/2));
            QTest::mouseClick(m_window,Qt::LeftButton,Qt::NoModifier,position.toPoint());
        } else if(phase>=8 && phase<=15) {
            auto* refresh=find(m_window->contentItem(),"scanRefreshButton");
            if(!refresh || refresh->isEnabled() ||
               !refresh->property("text").toString().startsWith("Scanning")) finish(false);
        } else if(phase==19) {
            auto* refresh=find(m_window->contentItem(),"scanRefreshButton");
            if(!refresh||!refresh->isEnabled()||refresh->property("text")!="Refresh"||
               !click("about")) finish(false);
        } else if(phase==20) {
            auto* build=find(m_window->contentItem(),"aboutBuildLabel");
            if(m_window->property("activeRoute").toString()!="about"||!build||
               !build->property("text").toString().contains("v51")||!capture("about-light")) {
                finish(false);return;
            }
            if(m_setTheme) m_setTheme(true);
        } else if(phase==21) {
            finish(capture("about-dark"));
        }
    }
    bool capture(const QString& name) {
        auto* glyph=find(m_window->contentItem(),"aboutBrandGlyph");
        const QColor expected=m_window->property("darkTheme").toBool()
            ? QColor("#24163E") : QColor("#FFFFFF");
        qInfo()<<"Theme foreground:"<<m_window->property("primaryForegroundColor")
               <<"glyph:"<<(glyph?glyph->property("color"):QVariant{});
        if(!glyph||glyph->property("color").value<QColor>()!=expected) return false;
        const QString path=qEnvironmentVariable("ARCVEIL_TEST_SCREENSHOTS");
        if(path.isEmpty()) return true;
        return QDir().mkpath(path)&&m_window->grabWindow().save(path+"/"+name+".png");
    }
    QQuickWindow* m_window;
    QObject* m_popup=nullptr;
    std::function<void(bool)> m_finished;
    std::function<void(bool)> m_setTheme;
    QTimer m_timer;
    int m_phase=0;
    int m_enterWaits=0;
};
