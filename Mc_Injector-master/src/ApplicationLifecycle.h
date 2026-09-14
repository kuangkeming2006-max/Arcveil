#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

// Explicit main-window closing owns session teardown. Transient windows do
// not decide application lifetime; OS/direct quit requests are logged normally.
class ApplicationLifecycle final : public QObject {
    Q_OBJECT
public:
    explicit ApplicationLifecycle(QObject* parent = nullptr);
    Q_INVOKABLE void requestExit(const QString& reason);
    Q_INVOKABLE void record(const QString& event);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    bool m_exitApproved = false;
    QFile m_log;
    QElapsedTimer m_heartbeatClock;
    QTimer m_heartbeat;
    unsigned m_pulses = 0;
};
