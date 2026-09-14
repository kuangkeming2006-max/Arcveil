#pragma once
#include <QObject>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QTest>
#include <functional>

// Explicit --smoke-test only. Delivers events to this owned QQuickWindow;
// never moves the system pointer or sends input to another application.
class ControllerUiSmoke final : public QObject {
public:
    ControllerUiSmoke(QQuickWindow* window, std::function<void(bool)> finished)
        : QObject(window), m_window(window), m_finished(std::move(finished)) {
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
            if(!m_popup->property("opened").toBool()) { finish(false); return; }
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
        } else if(phase>=19) {
            auto* refresh=find(m_window->contentItem(),"scanRefreshButton");
            finish(refresh && refresh->isEnabled() && refresh->property("text")=="Refresh");
        }
    }
    QQuickWindow* m_window;
    QObject* m_popup=nullptr;
    std::function<void(bool)> m_finished;
    QTimer m_timer;
    int m_phase=0;
};
