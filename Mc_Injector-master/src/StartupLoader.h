#pragma once

#include <QObject>
#include <QQmlComponent>
#include <QQmlIncubator>
#include <QQmlIncubationController>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <functional>

class QQmlApplicationEngine;
class QQuickWindow;

// A small, dependency-free window gets the first frame. Backend construction,
// QML compilation and object incubation then run as separate startup phases.
class StartupLoader final : public QObject {
public:
    StartupLoader(QQmlApplicationEngine& engine, std::function<void()> prepare,
                  std::function<void()> ready);
    ~StartupLoader() override;
    void start();
    QQuickWindow* mainWindow() const;

private:
    class Incubator final : public QQmlIncubator {
    public:
        explicit Incubator(StartupLoader& owner) : QQmlIncubator(Asynchronous), m_owner(owner) {}
        void statusChanged(Status status) override;
    private:
        StartupLoader& m_owner;
    };
    void loadMain();
    void componentReady();
    void showMain();
    void fail(const QString& message);

    QQmlApplicationEngine& m_engine;
    std::function<void()> m_prepare;
    std::function<void()> m_ready;
    QQmlComponent m_component;
    QQmlIncubationController m_startupIncubation;
    QTimer m_incubationPulse;
    Incubator m_incubator;
    QPointer<QQuickWindow> m_splash;
    QPointer<QQuickWindow> m_main;
    QElapsedTimer m_clock;
    bool m_creating = false;
    bool m_failed = false;
};
