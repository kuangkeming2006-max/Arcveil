#pragma once
#include "../mapping/cache/MappingCache.h"
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <functional>
class MappingService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY changed)
    Q_PROPERTY(QString logPath READ logPath NOTIFY changed)
  public:
    explicit MappingService(QObject *parent = nullptr);
    ~MappingService() override;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString fingerprint() const { return m_fingerprint; }
    QString logPath() const { return m_run + "/events.jsonl"; }
    void prepare(quint32 pid, const QString &java, const QString &helper, bool modular,
                 const QString &toolsJar);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool rollback();
  signals:
    void changed();
    void eventReceived(const QJsonObject &event);
    void ready(const QString &pack, const QString &digest);
    void failed(const QString &reason);

  private:
    friend struct MappingServiceTests;
    void launch(QStringList arguments, std::function<void(int)> finished);
    void inspect(bool finalCheck);
    void choosePack();
    void validate(const QString &pack, bool automatic);
    void resolve();
    void finalize();
    void fail(const QString &reason);
    void event(QJsonObject value);
    void stopProcess();
    QString m_root, m_tools, m_run, m_pack, m_java, m_helper, m_toolsJar, m_status, m_fingerprint,
        m_start, m_contractDigest;
    QString m_analyzer, m_contracts, m_defaultPack;
    QPointer<QProcess> m_process;
    QTimer m_timeout;
    quint64 m_generation = 0;
    quint32 m_pid = 0;
    bool m_busy = false, m_modular = true;
    QJsonObject m_validation, m_capture;
    mapping_cache::Entry m_hit;
};
