#pragma once
#include "../mapping/cache/MappingCache.h"
#include <QJsonObject>
#include <QObject>
#include "MappingEventModel.h"
#include "AttachTransaction.h"
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QJsonArray>
#include <QTimer>
#include <functional>
class MappingService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *events READ events CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool matchingEnabled READ matchingEnabled NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY changed)
    Q_PROPERTY(QString runtimeFingerprint READ fingerprint NOTIFY changed)
    Q_PROPERTY(QString logPath READ logPath NOTIFY changed)
  public:
    explicit MappingService(QObject *parent = nullptr);
    ~MappingService() override;
    QAbstractItemModel *events() { return &m_events; }
    double progress() const { return m_progress; }
    Q_INVOKABLE void clearEvents() { m_events.clear(); }
    bool busy() const { return m_busy; }
    bool matchingEnabled() const { return m_busy && m_watchEnabled; }
    Q_INVOKABLE void stopMatching();
    Q_INVOKABLE void resumeMatching();
    QString status() const { return m_status; }
    QString fingerprint() const { return m_fingerprint; }
    QString logPath() const { return m_run.isEmpty() ? QString{} : m_run + "/events.jsonl"; }
    void prepare(quint32 pid, const QString &java, const QString &helper, bool modular,
                 const QString &toolsJar, std::shared_ptr<AttachTransaction> transaction = {});
    static QString processStartFor(quint32 pid);
    QString transactionId() const { return m_transaction ? m_transaction->transactionId : QString{}; }
    QString selectedTransport() const { return m_transport; }
    void transactionEvent(const QString &type, const QString &reason = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool rollback();
  signals:
    void changed();
    void readyForTransaction(const QString &transactionId, const QString &pack, const QString &digest);
    void failedForTransaction(const QString &transactionId, const QString &reason);
    void eventReceived(const QJsonObject &event);
    void ready(const QString &pack, const QString &digest);
    void failed(const QString &reason);

  private:
    friend struct MappingServiceTests;
    friend struct TransactionCacheTests;
    friend struct LiveMappingSmoke;
    friend struct DynamicMappingServiceTests;
    enum class CapturePhase { Initial, Watch, Final, CacheWarmup };
    void captureLite(CapturePhase phase);
    void selectDetail(CapturePhase phase);
    void captureDetail(CapturePhase phase);
    void scheduleRetry(const QString &reason, bool unchanged = false);
    void watchLite();
    bool identityValid() const;
    bool captureFailed(int code);
    void persistMatchingState();
    void invalidate(const QString &symbol, const QString &reason);
    void provisional(QJsonObject value);
    void launch(QStringList arguments, std::function<void(int)> finished);
    void inspect(bool finalCheck);
    void identifyTarget();
    void selectCacheCandidate();
    void fallbackFromCache(const QString &reason);
    bool current() const;
    void choosePack();
    void validate(const QString &pack, bool automatic);
    void resolve();
    void finalize();
    void fail(const QString &reason);
    void event(QJsonObject value);
    void stopProcess();
    void progressSchema();
    void progressReference(const mapping_cache::Entry &reference);
    void progressAnalyzer(const QJsonObject &event);
    void progressVerified();
    QJsonObject m_progressContracts, m_progressSymbols;
    QString m_root, m_tools, m_run, m_pack, m_java, m_helper, m_toolsJar, m_status, m_fingerprint,
        m_start, m_contractDigest;
    QString m_analyzer, m_contracts, m_defaultPack, m_probe;
    QPointer<QProcess> m_process;
    void *m_processJob = nullptr;
    MappingEventModel m_events;
    double m_progress = -1;
    QTimer m_timeout;
    quint64 m_generation = 0;
    quint32 m_pid = 0;
    bool m_busy = false, m_modular = true;
    QJsonObject m_validation, m_capture;
    mapping_cache::Entry m_hit, m_reference;
    QTimer m_watchTimer;
    bool m_watchEnabled = false, m_dynamic = false, m_pendingChange = false;
    int m_watchInterval = 2500, m_maxWatchInterval = 5000, m_debounceMs = 750,
        m_captureFailures = 0;
    quint64 m_snapshotGeneration = 0, m_captureSerial = 0;
    QString m_snapshot, m_litePath, m_selectionPath, m_statePath, m_scopePack, m_observedLite,
        m_relevantFingerprint, m_nextRelevant, m_finalFingerprint;
    QJsonObject m_matchingState, m_provisional, m_analyzerFailure;
    QStringList m_unresolved;
    std::shared_ptr<AttachTransaction> m_transaction;
    QList<mapping_cache::Entry> m_candidates;
    int m_candidateIndex = 0;
    bool m_cachePath = false, m_candidateWarmed = false, m_stableAmbiguity = false, m_fullLite = false, m_authoredWarmup = false, m_forceAutomatic = false;
    QString m_identityPacksFile;
    QString m_family, m_minecraftVersion, m_mappingIdentity, m_metadataIdentity, m_transport;
    QHash<QString, QString> m_transports; // pid + processStart; never reused for a recycled PID.

};
