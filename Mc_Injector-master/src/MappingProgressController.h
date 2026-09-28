#pragma once
#include "MappingProgressModel.h"
#include <QVariantList>
#include <QVariantMap>
class MappingProgressController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *active READ active CONSTANT)
    Q_PROPERTY(QAbstractItemModel *completed READ completed CONSTANT)
    Q_PROPERTY(QAbstractItemModel *pending READ pending CONSTANT)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(QVariantMap reference READ reference NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool matchingEnabled READ matchingEnabled NOTIFY changed)
    Q_PROPERTY(bool successful READ successful NOTIFY changed)
  public:
    explicit MappingProgressController(QObject *parent = nullptr);
    QAbstractItemModel *active() { return &m_active; }
    QAbstractItemModel *completed() { return &m_completed; }
    QAbstractItemModel *pending() { return &m_pending; }
    int activeCount() const { return m_symbols.count("active"); }
    int completedCount() const { return m_symbols.count("completed"); }
    int pendingCount() const { return m_symbols.count("pending"); }
    QVariantList steps() const { return m_steps; }
    QVariantMap reference() const { return m_reference; }
    QString status() const { return m_status; }
    bool matchingEnabled() const { return m_matchingEnabled; }
    bool successful() const { return m_successful; }
    void begin(quint32 pid);
    void consume(const QJsonObject &event);
    Q_INVOKABLE void open() { emit openRequested(); }
    Q_INVOKABLE void stopMatching();
  signals:
    void changed();
    void openRequested();
    // Presentation retry lifecycle only, deliberately independent of preflight cancel/Agent detach.
    void matchingStopped();

  private:
    void step(int index, const QString &state);
    MappingProgressModel m_symbols;
    MappingProgressFilter m_active, m_completed, m_pending;
    QVariantList m_steps;
    QVariantMap m_reference;
    QString m_status, m_snapshot;
    bool m_matchingEnabled = false, m_successful = false;
};
