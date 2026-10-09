#include "MappingService.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUuid>
#include <memory>
#include <windows.h>
using namespace mapping_cache;
QString MappingService::processStartFor(quint32 pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    FILETIME created{}, exit{}, kernel{}, user{};
    const bool ok = GetProcessTimes(h, &created, &exit, &kernel, &user);
    DWORD code = 0;
    const bool active = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return ok && active
               ? QString::number((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime)
               : QString{};
}
MappingService::MappingService(QObject *parent) : QObject(parent) {
    m_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
             "/mapping-cache-v1";
    m_tools = QCoreApplication::applicationDirPath() + "/tools";
    m_analyzer = m_tools + "/MappingAnalyzer.exe";
    m_probe = m_tools + "/MappingProbe-v8.dll";
    m_contracts = m_tools + "/contracts-v1.json";
    m_defaultPack = QCoreApplication::applicationDirPath() + "/agent/mappings/default-v2.json";
    m_watchTimer.setSingleShot(true);
    m_timeout.setSingleShot(true);
}
MappingService::~MappingService() { cancel(); }
bool MappingService::current() const {
    return m_transaction && m_transaction->valid && m_transaction->pid == m_pid &&
           m_transaction->processStart == m_start;
}
void MappingService::transactionEvent(const QString &type, const QString &reason) {
    event({{"event", type}, {"reason", reason},
           {"state", m_transaction ? m_transaction->stateName() : QString("Idle")}});
}
void MappingService::event(QJsonObject value) {
    if (value.contains("fingerprint")) value["runtimeFingerprint"] = value.value("fingerprint");
    value["transactionId"] = transactionId();
    value["generation"] = double(m_generation);
    value["eventVersion"] = 1;
    value["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!m_run.isEmpty()) {
        QFile log(logPath());
        if (log.open(QIODevice::WriteOnly | QIODevice::Append))
            log.write(QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n');
    }
    m_events.append(value);
    if (value.value("event").toString() == "progress" && value.value("total").toInt() > 0) {
        m_progress = value.value("completed").toDouble() / value.value("total").toDouble();
        emit changed();
    }
    emit eventReceived(value);
}
void MappingService::stopProcess() {
    m_timeout.stop();
    if (m_processJob) { CloseHandle(m_processJob); m_processJob = nullptr; }
    if (!m_process)
        return;
    auto *job = m_process.data();
    m_process = nullptr;
    disconnect(job, nullptr, this, nullptr);
    if (job->state() == QProcess::NotRunning)
        job->deleteLater();
    else {
        job->setParent(nullptr);
        connect(job, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), job,
                &QObject::deleteLater);
        job->kill();
    }
}
void MappingService::cancel() {
    ++m_generation;
    if (m_busy && m_transaction) m_transaction->invalidate();
    m_watchTimer.stop();
    m_watchEnabled = false;
    stopProcess();
    if (m_busy) {
        m_busy = false;
        m_status = "Cancelled";
        const auto generation = m_generation;
        transactionEvent("TRANSACTION_CANCEL", "Cancel Attach");
        if (generation != m_generation) return;
        event({{"event", "cancelled"}});
        if (generation != m_generation) return;
        emit changed();
    }
}
void MappingService::fail(const QString &reason) {
    ++m_generation;
    if (m_busy && m_transaction) m_transaction->invalidate();
    m_watchTimer.stop();
    m_watchEnabled = false;
    stopProcess();
    m_busy = false;
    m_status = reason;
    const auto generation = m_generation;
    const auto id = transactionId();
    if (m_transaction) m_transaction->state = AttachTransaction::State::Failed;
    try {
        Cache(m_root).candidate(m_run, m_fingerprint, "failed");
    } catch (...) {
    }
    event({{"event", "failure"}, {"reason", reason}});
    if (generation != m_generation) return;
    emit changed();
    if (generation != m_generation) return;
    emit failedForTransaction(id, reason);
    if (generation != m_generation) return;
    emit failed(reason);
}
void MappingService::prepare(quint32 pid, const QString &java, const QString &helper, bool modular,
                             const QString &toolsJar, std::shared_ptr<AttachTransaction> transaction) {
    cancel();
    m_events.clear();
    m_progress = -1;
    m_pid = pid;
    m_transaction = transaction ? std::move(transaction) : std::make_shared<AttachTransaction>();
    m_transaction->pid = pid;
    m_transaction->state = AttachTransaction::State::MappingFastPath;
    m_cachePath = false; m_fullLite = false; m_authoredWarmup = false; m_forceAutomatic = false;
    m_stableAmbiguity = false;
    m_candidates.clear();
    m_candidateIndex = 0; m_candidateWarmed = false;
    m_family.clear(); m_minecraftVersion.clear(); m_mappingIdentity.clear(); m_metadataIdentity.clear();
    m_java = java;
    m_helper = helper;
    m_modular = modular;
    m_toolsJar = toolsJar;
    m_hit = {};
    m_reference = {};
    m_matchingState = {};
    m_provisional = {};
    m_unresolved.clear();
    m_dynamic = false;
    m_pendingChange = false;
    m_watchEnabled = true;
    m_captureFailures = 0;
    m_snapshotGeneration = 0;
    m_captureSerial = 0;
    m_observedLite.clear();
    m_relevantFingerprint.clear();
    m_statePath.clear();
    m_snapshot.clear();
    m_scopePack = m_defaultPack;
    m_validation = {};
    m_capture = {};
    m_fingerprint.clear();
    m_pack.clear();
    m_start = processStartFor(pid);
    if (m_transaction->processStart.isEmpty()) m_transaction->processStart = m_start;
    m_transport = m_transports.value(QString::number(pid) + ":" + m_start);
    m_run = m_root + "/runs/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_busy = true;
    QDir().mkpath(m_run);
    transactionEvent("TRANSACTION_BEGIN");
    event({{"event", "session-start"}, {"pid", double(pid)}});
    event({{"event", "step"},
           {"index", 0},
           {"state", "running"},
           {"message", "Checking local cache index"}});
    m_status = "Checking runtime mappings";
    emit changed();
    try {
        if (m_start.isEmpty() || !QFile::exists(m_analyzer) || !QDir().mkpath(m_run))
            throw std::runtime_error("mapping analyzer/runtime unavailable");
        m_contractDigest = fileDigest(m_contracts);
        m_identityPacksFile = m_run + "/identity-packs.json";
        writeObject(m_identityPacksFile, {{"packs", QJsonArray::fromStringList(Cache(m_root).knownPacks(m_contractDigest))}});
        progressSchema();
        event({{"event", "step"},
               {"index", 0},
               {"state", "success"},
               {"message", QFile::exists(m_root + "/index.json")
                               ? "Local cache found; family and stable identity detection follows"
                               : "No local cache; capturing runtime fingerprint"}});
        Cache(m_root).candidate(m_run, {}, "inspecting");
        inspect(false);
    } catch (const std::exception &e) {
        fail(QString::fromUtf8(e.what()));
    }
}
void MappingService::launch(QStringList arguments, std::function<void(int)> finished) {
    if (!m_busy || !current()) return;
    m_analyzerFailure = {};
    struct Output {
        QByteArray bytes;
        qsizetype total = 0;
        bool scheduled = false, exited = false, done = false;
        int code = 0;
        QProcess::ExitStatus status = QProcess::NormalExit;
    };
    auto *job = new QProcess(this);
    m_process = job;
    const auto generation = m_generation;
    auto output = std::make_shared<Output>();
    auto drain = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakDrain = drain;
    *drain = [this, job, generation, output, weakDrain, finished = std::move(finished)] {
        if (generation != m_generation || !current() || output->done)
            return;
        output->scheduled = false;
        auto received = job->readAllStandardOutput();
        output->total += received.size();
        output->bytes += received;
        if (output->bytes.size() > 2 * 1024 * 1024 || output->total > 16 * 1024 * 1024) {
            output->done = true;
            fail("Analyzer event stream exceeded limits");
            return;
        }
        QElapsedTimer budget;
        budget.start();
        int delivered = 0;
        for (; delivered < 32 && budget.elapsed() < 5; ++delivered) {
            auto end = output->bytes.indexOf('\n');
            if (end < 0)
                break;
            auto line = output->bytes.left(end);
            output->bytes.remove(0, end + 1);
            if (line.trimmed().isEmpty())
                continue;
            auto doc = QJsonDocument::fromJson(line);
            if (!doc.isObject() || doc.object().value("eventVersion").toInt() != 1) {
                output->done = true;
                fail("Invalid analyzer JSONL event");
                return;
            }
            const auto object = doc.object();
            if (object.value("event") == "CAPTURE_TRANSPORT_SELECTED") {
                m_transport = object.value("transport").toString();
                if (m_transport == "NativeLoader")
                    m_transports[QString::number(m_pid) + ":" + m_start] = m_transport;
            }
            if (object.value("event").toString() == "fingerprint")
                m_capture = object;
            if (object.value("event") == "failure") {
                m_analyzerFailure = object;
                auto diagnostic = object;
                diagnostic["event"] = "analyzer-diagnostic";
                event(diagnostic);
            } else {
                event(object);
                if (generation != m_generation || !current()) return;
                progressAnalyzer(object);
            }
            if (generation != m_generation)
                return;
        }
        if (output->bytes.contains('\n')) {
            if (auto next = weakDrain.lock()) {
                output->scheduled = true;
                QTimer::singleShot(0, this, [next] { (*next)(); });
            }
            return;
        }
        if (!output->exited)
            return;
        output->done = true;
        m_timeout.stop();
        if (m_processJob) { CloseHandle(m_processJob); m_processJob = nullptr; }
        m_process = nullptr;
        job->deleteLater();
        if (output->status != QProcess::NormalExit || !output->bytes.trimmed().isEmpty()) {
            fail("Analyzer crashed or returned truncated JSONL");
            return;
        }
        try {
            finished(output->code);
        } catch (const std::exception &e) {
            if (generation == m_generation && current()) fail(QString::fromUtf8(e.what()));
        }
    };
    const auto schedule = [this, output, drain] {
        if (!output->scheduled && !output->done) {
            output->scheduled = true;
            QTimer::singleShot(0, this, [drain] { (*drain)(); });
        }
    };
    connect(job, &QProcess::readyReadStandardOutput, this, schedule);
    connect(job, &QProcess::readyReadStandardError, this,
            [job] { (void)job->readAllStandardError(); });
    connect(job, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError error) {
        if (generation == m_generation && error == QProcess::FailedToStart)
            fail("MappingAnalyzer could not start");
    });
    connect(job, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [output, schedule](int code, QProcess::ExitStatus status) {
                output->code = code;
                output->status = status;
                output->exited = true;
                schedule();
            });
    connect(job, &QProcess::started, this, [this, job, generation] {
        if (generation != m_generation || !current()) return;
        auto group = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        auto process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(job->processId()));
        if (group && process && SetInformationJobObject(group, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
            AssignProcessToJobObject(group, process)) m_processJob = group;
        else if (group) CloseHandle(group);
        if (process) CloseHandle(process);
    });
    job->setProgram(m_analyzer);
    job->setArguments(arguments);
    job->start();
    disconnect(&m_timeout, nullptr, this, nullptr);
    connect(&m_timeout, &QTimer::timeout, this, [this, generation] {
        if (generation == m_generation && current()) fail("Mapping analysis timed out");
    });
    m_timeout.start(120000);
}

bool MappingService::identityValid() const {
    return current() && processStartFor(m_pid) == m_start && !m_start.isEmpty();
}
void MappingService::inspect(bool finalCheck) {
    captureLite(finalCheck ? CapturePhase::Final : CapturePhase::Initial);
}
void MappingService::finalize() {
    const auto owner = m_transaction;
    const auto generation = m_generation;
    if (!m_busy || !identityValid())
        throw std::runtime_error("target JVM exited before injection");
    if (!m_validation.value("valid").toBool() || !m_validation.value("injectionReady").toBool())
        throw std::runtime_error("finalize requires current live validation");
    if (m_mappingIdentity.isEmpty() || m_family.isEmpty() || m_family == "Unknown")
        throw std::runtime_error("stable mapping identity/family unavailable");
    if (!m_hit.valid()) {
        m_hit = Cache(m_root).promote(m_pack, m_snapshot, m_fingerprint, m_contractDigest,
                                      m_validation, m_mappingIdentity, m_metadataIdentity,
                                      m_family, m_minecraftVersion);
        event({{"event", "CACHE_PROMOTE"}, {"cacheKey", m_mappingIdentity},
               {"mappingIdentity", m_mappingIdentity}, {"revision", m_hit.revision}});
    }
    if (owner != m_transaction || generation != m_generation || !current()) return;
    m_transaction->state = AttachTransaction::State::MappingVerified;
    event({{"event", "validation"},
           {"valid", true},
           {"stage", "verified"},
           {"fingerprint", m_fingerprint},
           {"pack", m_hit.pack},
           {"digest", m_hit.digest}});
    if (owner != m_transaction || generation != m_generation || !current()) return;
    m_watchEnabled = false;
    m_watchTimer.stop();
    m_busy = false;
    m_status = m_cachePath ? "Cache HIT -> Live validation -> Ready" : "Mapping verified -> Ready";
    progressVerified();
    m_progress = 1;
    emit changed();
    if (owner != m_transaction || generation != m_generation || !current()) return;
    emit readyForTransaction(transactionId(), m_hit.pack, m_hit.digest);
    if (owner == m_transaction && generation == m_generation && current()) emit ready(m_hit.pack, m_hit.digest);
}
bool MappingService::rollback() {
    if (m_busy)
        return false;
    try {
        bool ok = Cache(m_root).rollback();
        event({{"event", "rollback"}, {"success", ok}});
        return ok;
    } catch (const std::exception &e) {
        event({{"event", "failure"}, {"reason", QString::fromUtf8(e.what())}});
        return false;
    }
}
