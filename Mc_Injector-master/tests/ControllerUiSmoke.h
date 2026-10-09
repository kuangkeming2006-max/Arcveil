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
#include "../src/MappingProgressController.h"

// Explicit --smoke-test only. Delivers events to this owned QQuickWindow;
// never moves the system pointer or sends input to another application.
class ControllerUiSmoke final : public QObject {
public:
    ControllerUiSmoke(QQuickWindow* window, std::function<void(bool)> finished,
        std::function<void(bool)> setTheme = {}, MappingProgressController* progress=nullptr, OverlayManager* overlay=nullptr)
        : QObject(window), m_window(window), m_finished(std::move(finished)),
          m_setTheme(std::move(setTheme)), m_progress(progress), m_overlay(overlay) {
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
               !build->property("text").toString().contains("v56.6")||!capture("about-light")) {
                finish(false);return;
            }
            if(m_setTheme) m_setTheme(true);
        } else if(phase==21) {
            if (!capture("about-dark")) { finish(false); return; }
            m_window->setProperty("activeRoute", "scanner");
        } else if (phase >= 22 && phase <= 28) {
            // All seven pages remain eagerly instantiated after component extraction.
            // Session-only routes are selected directly in this isolated smoke run.
            const QStringList routes{"scanner", "config", "main", "hypixel",
                                     "player", "about", "settings"};
            const int index = phase - 22;
            if (m_window->property("activeRoute").toString() != routes[index] ||
                !capture("page-" + routes[index])) { finish(false); return; }
            if (index + 1 < routes.size())
                m_window->setProperty("activeRoute", routes[index + 1]);
        } else if (phase == 29) {
            m_popup = m_window->findChild<QObject*>("mappingConsole");
            auto* button = find(m_window->contentItem(), "openMappingConsoleButton");
            if (!m_popup || m_popup->property("visible").toBool() || !button || !button->isVisible()) { finish(false); return; }
            QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
        } else if (phase == 30) {
            if (!m_popup->property("opened").toBool() || !capture("mapping-console-dark")) { finish(false); return; }
            if (m_setTheme) m_setTheme(false);
        } else if (phase == 31) {
            if (!capture("mapping-console-light")) { finish(false); return; }
            QMetaObject::invokeMethod(m_popup,"close");
        } else if (phase == 32) {
            if(m_popup->property("visible").toBool() || !m_progress || !m_overlay) {finish(false);return;}
            m_progressWindow=m_window->findChild<QQuickWindow*>("mappingProgressWindow");
            if(!m_progressWindow || m_progressWindow->isVisible()) {finish(false);return;}
            m_progress->begin(42); // Presentation begins without a target JVM/Attach transaction.
        } else if(phase==33) {
            if(!m_progressWindow->isVisible()) {finish(false);return;}
            m_progress->begin(42);
            m_progress->consume({{"event","step"},{"index",0},{"state","success"}});
            m_progress->consume({{"event","step"},{"index",1},{"state","success"}});
            m_progress->consume({{"event","step"},{"index",2},{"state","running"}});
            m_progress->consume({{"event","reference"},{"referenceAvailable",true},{"sourceClientFamily","Lunar"},
                {"sourceFingerprint","reference-verified-8c913e"},{"sourcePack","cache/verified/pack.json"},
                {"sourceSnapshot","cache/verified/snapshot.json"},{"targetClientFamily","Lunar"},
                {"targetFingerprint","runtime-43a1f9"},{"selectionReason","UI fixture: verified reference with matching contracts"}});
            for(const auto &key:QStringList{"Minecraft.thePlayer","World.players","Entity.position"})
                m_progress->consume({{"event","symbol-queued"},{"symbol",key},{"required",key!="Entity.position"}});
            m_progress->consume({{"event","symbol-started"},{"symbol","Minecraft.thePlayer"}});
        } else if(phase==34) {
            if(!captureProgress("mapping-progress-active")) {finish(false);return;}
            m_progress->consume({{"event","symbol-matched"},{"symbol","Minecraft.thePlayer"},
                {"runtimeName","ave.f"},{"verified",false},{"provisional",true},{"confidence",0.99},
                {"evidence",QJsonArray{"Unique hierarchy, descriptor and normalized bytecode match"}}});
        } else if(phase==35) {
            if(m_progress->activeCount()!=1 || !captureProgress("mapping-progress-migrating")) {finish(false);return;}
        } else if(phase==36) {
            if(m_progress->completedCount()!=1) {finish(false);return;}
            const auto state=m_overlay->state();
            auto* stop=find(m_progressWindow->contentItem(),"mappingStopMatching");
            if(!stop || !stop->isEnabled()) {finish(false);return;}
            QTest::mouseClick(m_progressWindow,Qt::LeftButton,Qt::NoModifier,
                stop->mapToScene(QPointF(stop->width()/2,stop->height()/2)).toPoint());
            if(m_progress->matchingEnabled() || m_overlay->state()!=state) {finish(false);return;}
            m_progressWindow->close();
        } else if(phase==37) {
            if(m_progressWindow->isVisible()) {finish(false);return;}
            m_progress->consume({{"event","symbol-matched"},{"symbol","World.players"},{"runtimeName","bdb.j"},{"verified",false},{"provisional",true},{"confidence",0.99}});
            if(m_progress->successful()) {finish(false);return;}
            m_progress->consume({{"event","symbol-matched"},{"symbol","Minecraft.thePlayer"},{"runtimeName","ave.f"},{"verified",true}});
            m_progress->consume({{"event","symbol-matched"},{"symbol","World.players"},{"runtimeName","bdb.j"},{"verified",true}});
            m_progress->open();
        } else if(phase==38) {
            if(!m_progressWindow->isVisible() || !m_progress->successful() || !captureProgress("mapping-progress-success-light")) {finish(false);return;}
            if(m_setTheme) m_setTheme(true);
        } else if(phase==39) {
            if(!captureProgress("mapping-progress-success-dark")) {finish(false);return;}
            m_progressWindow->close();
            finish(true);
        }
    }
    bool captureProgress(const QString& name) {
        const auto path=qEnvironmentVariable("ARCVEIL_TEST_SCREENSHOTS");
        return path.isEmpty() || (QDir().mkpath(path) && m_progressWindow->grabWindow().save(path+"/"+name+".png"));
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
    QQuickWindow* m_progressWindow=nullptr;
    MappingProgressController* m_progress=nullptr;
    OverlayManager* m_overlay=nullptr;
    QObject* m_popup=nullptr;
    std::function<void(bool)> m_finished;
    std::function<void(bool)> m_setTheme;
    QTimer m_timer;
    int m_phase=0;
    int m_enterWaits=0;
};
