#include "ApplicationLifecycle.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QStandardPaths>
#include <QDebug>
#include <QWindow>

ApplicationLifecycle::ApplicationLifecycle(QObject* parent) : QObject(parent)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/diagnostics");
    QDir().mkpath(directory);
    // One log per run: no user files are overwritten and no credentials/player
    // identities are logged. Entries describe lifecycle and event-loop stalls.
    m_log.setFileName(directory + QStringLiteral("/session-%1-%2.log")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")))
        .arg(QCoreApplication::applicationPid()));
    if (!m_log.open(QIODevice::WriteOnly | QIODevice::Text))
        qWarning() << "Could not open lifecycle diagnostic log:" << m_log.errorString();
    record(QStringLiteral("started"));
    qApp->installEventFilter(this);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    connect(qApp, &QGuiApplication::lastWindowClosed, this, [this] {
        record(QStringLiteral("lastWindowClosed; automatic exit suppressed"));
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        record(m_exitApproved ? QStringLiteral("aboutToQuit: explicit exit")
                              : QStringLiteral("aboutToQuit: test/OS/direct exit"));
    });
    m_heartbeatClock.start();
    m_heartbeat.setInterval(1000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        const auto elapsed = m_heartbeatClock.restart();
        if (elapsed > 2500) record(QStringLiteral("event loop delayed %1 ms").arg(elapsed));
        if (++m_pulses % 5 == 0) record(QStringLiteral("GUI event loop alive"));
    });
    m_heartbeat.start();
}

void ApplicationLifecycle::record(const QString& event)
{
    if (!m_log.isOpen()) return;
    const auto line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QLatin1Char(' ') + event.left(240) + QLatin1Char('\n');
    m_log.write(line.toUtf8());
    m_log.flush();
}

void ApplicationLifecycle::requestExit(const QString& reason)
{
    if (m_exitApproved) return;
    record(QStringLiteral("exit requested: ") + reason);
    m_exitApproved = true;
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

bool ApplicationLifecycle::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress && qobject_cast<QWindow*>(watched))
        record(QGuiApplication::modalWindow() ? QStringLiteral("mouse press delivered; native modal active")
                                             : QStringLiteral("mouse press delivered to window"));
    if (watched == qApp && event->type() == QEvent::Quit && !m_exitApproved) {
        // Observe unexpected exits without swallowing legitimate OS/Qt quit
        // requests. A blanket Quit filter can itself strand a hidden process.
        record(QStringLiteral("Quit event without an explicit UI exit request"));
    }
    return QObject::eventFilter(watched, event);
}
