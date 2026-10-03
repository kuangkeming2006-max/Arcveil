#include "MappingService.h"
#include <QFile>
#include <QDir>
using namespace mapping_cache;

void MappingService::stopMatching() {
    m_watchEnabled = false;
    m_watchTimer.stop();
    // In-flight capture/resolve/final validation owns its current generation.
    // Stopping watch neither cancels that job nor changes OverlayManager's state.
    event({{"event", "snapshot-watch"},
           {"state", "paused"},
           {"reason", "User stopped snapshot watch/retry"}});
    emit changed();
}
void MappingService::resumeMatching() {
    if (!m_busy || m_watchEnabled)
        return;
    m_watchEnabled = true;
    event({{"event", "snapshot-watch"}, {"state", "running"}});
    emit changed();
    if (!m_process) {
        m_watchTimer.start(m_watchInterval);
    }
}
void MappingService::scheduleRetry(const QString &reason, bool unchanged) {
    if (!m_busy)
        return;
    m_status =
        m_watchEnabled ? "Waiting for runtime change" : "Matching paused; current attach retained";
    event({{"event", "waiting-for-runtime-change"},
           {"reason", reason},
           {"unresolved", QJsonArray::fromStringList(m_unresolved)},
           {"matched", m_provisional.size()},
           {"paused", !m_watchEnabled}});
    if (m_watchEnabled) {
        event({{"event", "retry-scheduled"},
               {"delayMs", m_watchInterval},
               {"generation", double(m_snapshotGeneration)}});
        m_watchTimer.start(m_watchInterval);
        if (unchanged)
            m_watchInterval = qMin(m_maxWatchInterval, m_watchInterval * 2);
    }
    emit changed();
}
void MappingService::watchLite() {
    if (!m_busy || !m_watchEnabled || m_process)
        return;
    if (!identityValid()) {
        fail("Target JVM exited or changed identity");
        return;
    }
    event({{"event", "snapshot-watch"},
           {"state", "running"},
           {"detailLevel", "lite"},
           {"intervalMs", m_watchInterval}});
    captureLite(CapturePhase::Watch);
}
bool MappingService::captureFailed(int code) {
    if (!code) {
        m_captureFailures = 0;
        return false;
    }
    if (!identityValid()) {
        fail("Target JVM exited during capture");
        return true;
    }
    m_pendingChange = true;
    if (code == 5 || m_analyzerFailure.value("retryable").toBool()) {
        event({{"event", "snapshot-changed"}, {"reason", "Runtime changed during capture"}});
        scheduleRetry("Capture observed runtime drift", true);
    } else if (++m_captureFailures < 3)
        scheduleRetry("Capture temporarily unavailable", true);
    else
        fail("Capture cannot continue: " +
             m_analyzerFailure.value("reason").toString("Analyzer capture failed"));
    return true;
}
void MappingService::captureLite(CapturePhase phase) {
    if (!identityValid()) {
        fail("Target JVM exited or changed identity");
        return;
    }
    m_capture = {};
    ++m_captureSerial;
    m_litePath = m_run + QString("/index-%1.jsonl").arg(m_captureSerial);
    if (phase == CapturePhase::Initial) {
        m_reference = Cache(m_root).reference(m_contractDigest);
        m_scopePack = m_reference.valid() ? m_reference.pack : m_defaultPack;
        progressReference(m_reference);
        event({{"event", "step"},
               {"index", 1},
               {"state", "running"},
               {"message", "Capturing lite runtime index"}});
    } else if (phase == CapturePhase::Final) {
        m_finalFingerprint = m_fingerprint;
        event({{"event", "step"},
               {"index", 3},
               {"state", "running"},
               {"message", "Final live identity/fingerprint check"}});
    }
    QStringList args = {
        "inspect", "--lite",  "--pid",           QString::number(m_pid),
        "--java",  m_java,    "--helper",        m_helper,
        "--probe", m_probe,   "--native-loader", m_tools + "/McOverlayNativeLoader.exe",
        "--out",   m_litePath};
    if (phase == CapturePhase::Initial)
        args << "--pack" << m_defaultPack << "--contracts" << m_contracts;
    if (!m_modular)
        args << "--tools-jar" << m_toolsJar;
    launch(args, [this, phase](int code) {
        if (captureFailed(code))
            return;
        if (!identityValid() || m_capture.value("processStart").toString() != m_start ||
            m_capture.value("pid").toDouble() != m_pid) {
            fail("Target JVM identity mismatch");
            return;
        }
        const auto fingerprint = m_capture.value("fingerprint").toString();
        if (fingerprint.isEmpty()) {
            fail("Capture returned no fingerprint");
            return;
        }
        if (phase == CapturePhase::Watch) {
            const auto reference = Cache(m_root).reference(m_contractDigest);
            if (reference.revision != m_reference.revision && reference.valid()) {
                m_reference = reference;
                m_scopePack = reference.pack;
                m_matchingState = {};
                m_statePath.clear();
                m_pendingChange = true;
                progressReference(reference);
            }
            if (fingerprint == m_observedLite && !m_pendingChange) {
                event({{"event", "snapshot-unchanged"},
                       {"fingerprint", fingerprint},
                       {"detailSkipped", true}});
                scheduleRetry("Lite fingerprint unchanged", true);
                return;
            }
            if (!m_watchEnabled) {
                m_pendingChange = true;
                scheduleRetry("Watch stopped after lite capture");
                return;
            }
        }
        m_observedLite = fingerprint;
        selectDetail(phase);
    });
}
void MappingService::selectDetail(CapturePhase phase) {
    m_selectionPath = m_run + QString("/selection-%1.json").arg(m_captureSerial);
    // Preserve authored-pack bootstrap across client families. The reference pack
    // describes source names; it must not hide classes available in the target pack.
    const auto selectionPack = m_dynamic ? m_scopePack : m_defaultPack;
    QStringList args = {"select",   "--allow-empty", "--pack",    selectionPack, "--snapshot",
                        m_litePath, "--contracts",   m_contracts, "--out",       m_selectionPath};
    if (m_reference.valid())
        args << "--reference" << m_reference.snapshot;
    launch(args, [this, phase](int code) {
        if (code) {
            fail("Analyzer cannot select detail scope: " +
                 m_analyzerFailure.value("reason").toString());
            return;
        }
        const auto selection = readObject(m_selectionPath);
        m_nextRelevant = selection.value("relevantFingerprint").toString();
        if (m_nextRelevant.isEmpty()) {
            fail("Selection omitted relevant fingerprint");
            return;
        }
        if (phase == CapturePhase::Watch && m_nextRelevant == m_relevantFingerprint &&
            !m_pendingChange) {
            event({{"event", "snapshot-changed"},
                   {"fingerprint", m_observedLite},
                   {"relevantChanged", false},
                   {"detailSkipped", true}});
            scheduleRetry("No relevant class/metadata change", true);
            return;
        }
        if (selection.value("classes").toArray().isEmpty()) {
            m_relevantFingerprint = m_nextRelevant;
            m_pendingChange = false;
            scheduleRetry("No detail candidates yet", true);
            return;
        }
        if (phase == CapturePhase::Watch) {
            m_pendingChange = true;
            event({{"event", "snapshot-changed"},
                   {"fingerprint", m_observedLite},
                   {"relevantChanged", true}});
            const auto operation = m_generation, serial = m_captureSerial;
            QTimer::singleShot(m_debounceMs, this, [this, operation, serial] {
                if (operation != m_generation || serial != m_captureSerial || !m_busy)
                    return;
                if (m_watchEnabled)
                    captureDetail(CapturePhase::Watch);
                else
                    scheduleRetry("Watch stopped during debounce");
            });
        } else
            captureDetail(phase);
    });
}
void MappingService::captureDetail(CapturePhase phase) {
    m_capture = {};
    const auto output = m_run + QString("/detail-%1.jsonl").arg(m_captureSerial);
    QStringList args = {"inspect-detail",
                        "--pid",
                        QString::number(m_pid),
                        "--java",
                        m_java,
                        "--helper",
                        m_helper,
                        "--probe",
                        m_probe,
                        "--native-loader",
                        m_tools + "/McOverlayNativeLoader.exe",
                        "--candidates",
                        m_selectionPath,
                        "--out",
                        output};
    if (!m_modular)
        args << "--tools-jar" << m_toolsJar;
    launch(args, [this, phase, output](int code) {
        if (captureFailed(code))
            return;
        if (!identityValid() || m_capture.value("pid").toDouble() != m_pid ||
            m_capture.value("processStart").toString() != m_start) {
            fail("Detail capture identity mismatch");
            return;
        }
        const auto fp = m_capture.value("fingerprint").toString();
        if (fp.isEmpty()) {
            fail("Detail capture omitted fingerprint");
            return;
        }
        if (phase == CapturePhase::Final && fp == m_finalFingerprint) {
            finalize();
            return;
        }
        m_snapshot = output;
        m_fingerprint = fp;
        m_relevantFingerprint = m_nextRelevant;
        m_pendingChange = false;
        ++m_snapshotGeneration;
        m_watchInterval = qMin(m_watchInterval, 2500);
        event({{"event", "snapshot-update"},
               {"fingerprint", fp},
               {"generation", double(m_snapshotGeneration)}});
        emit changed();
        if (phase == CapturePhase::Final) {
            event({{"event", "snapshot-changed"},
                   {"reason", "final-fingerprint-drift"},
                   {"generation", double(m_snapshotGeneration)}});
            m_hit = {};
            m_validation = {};
            if (!m_watchEnabled) {
                m_pendingChange = true;
                scheduleRetry("Final drift; retry paused");
                return;
            }
            if (m_dynamic)
                resolve();
            else
                validate(m_pack.isEmpty() ? m_defaultPack : m_pack, false);
        } else if (phase == CapturePhase::Initial) {
            event({{"event", "step"}, {"index", 1}, {"state", "success"}});
            choosePack();
        } else {
            event({{"event", "retry-started"},
                   {"generation", double(m_snapshotGeneration)},
                   {"unresolved", QJsonArray::fromStringList(m_unresolved)}});
            if (m_dynamic)
                resolve();
            else
                choosePack();
        }
    });
}
void MappingService::choosePack() {
    m_hit = Cache(m_root).lookup(m_fingerprint, m_contractDigest);
    event({{"event", "cache"},
           {"stage", "verified"},
           {"hit", m_hit.valid()},
           {"fingerprint", m_fingerprint}});
    if (m_hit.valid()) {
        m_pack = m_hit.pack;
        inspect(true);
    } else
        validate(m_defaultPack, false);
}
void MappingService::validate(const QString &pack, bool automatic) {
    m_status = "Full live mapping validation";
    emit changed();
    event({{"event", "step"},
           {"index", 2},
           {"state", "running"},
           {"message", "Full live validation; provisional results cannot authorize injection"}});
    event({{"event", "pack"}, {"path", pack}, {"automatic", automatic}});
    launch({"validate", "--pack", pack, "--snapshot", m_snapshot, "--contracts", m_contracts,
            "--out", m_run + "/validation.json"},
           [this, automatic](int code) {
               if (code != 0 && code != 3) {
                   fail("Live validation cannot continue");
                   return;
               }
               m_validation = readObject(m_run + "/validation.json");
               QStringList rejected;
               if (automatic) {
                   auto rows = m_validation.value("symbols").toArray();
                   for (const auto &attempt : m_validation.value("attempts").toArray())
                       for (const auto &row : attempt.toObject().value("symbols").toArray())
                           rows.append(row);
                   const auto accepted = m_matchingState.value("accepted").toObject();
                   for (const auto &row : rows) {
                       const auto value = row.toObject();
                       const auto key = value.value("symbol").toString();
                       const auto mapping = accepted.value(key).toObject().value("mapping");
                       bool nonempty = !mapping.toString().isEmpty();
                       for (const auto &name : mapping.toArray())
                           nonempty |= !name.toString().isEmpty();
                       // Authored optional omissions may fail member lookup. Every nonempty
                       // automatic binding must validate, even if only optional contracts fail.
                       if (nonempty && !value.value("accepted").toBool())
                           rejected << key;
                   }
               }
               if (code || !m_validation.value("injectionReady").toBool() || !rejected.isEmpty()) {
                   if (!automatic) {
                       resolve();
                       return;
                   }
                   for (const auto &key : rejected)
                       invalidate(key, "Full live validation rejected binding");
                   persistMatchingState();
                   scheduleRetry("Full live validation incomplete");
                   return;
               }
               if (m_validation.value("fingerprint").toString() != m_fingerprint) {
                   fail("Invalid validation receipt identity");
                   return;
               }
               if (automatic) {
                   auto results = m_validation.value("symbols").toArray();
                   for (int i = 0; i < results.size(); ++i) {
                       auto row = results[i].toObject();
                       const auto source =
                           m_progressSymbols.value(row.value("symbol").toString()).toObject();
                       if (!source.isEmpty()) {
                           row["confidence"] = source.value("confidence");
                           row["evidence"] = source.value("evidence");
                           results[i] = row;
                       }
                   }
                   m_validation["symbols"] = results;
               }
               m_pack = m_run + "/validated-pack.json";
               writeObject(m_pack, m_validation.value("pack").toObject());
               Cache(m_root).candidate(m_run, m_fingerprint, "validated-awaiting-recheck");
               inspect(true);
           });
}
void MappingService::persistMatchingState() {
    if (m_matchingState.isEmpty())
        return;
    m_statePath = m_run + "/matching-state.json";
    writeObject(m_statePath, m_matchingState);
}
void MappingService::invalidate(const QString &symbol, const QString &reason) {
    if (!m_provisional.contains(symbol))
        return;
    m_provisional.remove(symbol);
    auto accepted = m_matchingState.value("accepted").toObject();
    accepted.remove(symbol);
    m_matchingState["accepted"] = accepted;
    if (!m_unresolved.contains(symbol))
        m_unresolved << symbol;
    m_matchingState["unresolved"] = QJsonArray::fromStringList(m_unresolved);
    m_matchingState["requiredComplete"] = false;
    event({{"event", "symbol-invalidated"}, {"symbol", symbol}, {"reason", reason}});
}
void MappingService::resolve() {
    m_dynamic = true;
    m_status = "Incremental runtime matching";
    emit changed();
    event({{"event", "step"},
           {"index", 2},
           {"state", "running"},
           {"message", "Matching unresolved / affected symbols; retaining revalidated bindings"}});
    progressReference(m_reference);
    QStringList args = {
        "resolve",  "--incremental", "--pack",    m_scopePack, "--snapshot",
        m_snapshot, "--contracts",   m_contracts, "--out",     m_run + "/candidate.json"};
    if (m_reference.valid())
        args << "--reference" << m_reference.snapshot;
    if (!m_statePath.isEmpty())
        args << "--state" << m_statePath;
    if (m_snapshotGeneration > 1)
        for (const auto &key : m_unresolved)
            event({{"event", "symbol-retry"},
                   {"symbol", key},
                   {"generation", double(m_snapshotGeneration)}});
    Cache(m_root).candidate(m_run, m_fingerprint, "analyzing");
    launch(args, [this](int code) {
        if (code != 0 && code != 4) {
            fail("Incremental Analyzer cannot continue");
            return;
        }
        const auto candidate = readObject(m_run + "/candidate.json");
        if (candidate.value("stateVersion").toInt() != 1 ||
            candidate.value("fingerprint").toString() != m_fingerprint) {
            fail("Invalid incremental candidate receipt");
            return;
        }
        const auto accepted = candidate.value("accepted").toObject();
        const auto prior = m_provisional;
        for (auto it = prior.begin(); it != prior.end(); ++it)
            if (!accepted.contains(it.key()))
                invalidate(it.key(), "Candidate no longer has unique valid evidence");
        m_matchingState = candidate;
        m_unresolved.clear();
        for (const auto &key : candidate.value("unresolved").toArray())
            m_unresolved << key.toString();
        m_progressSymbols = accepted;
        for (auto it = accepted.begin(); it != accepted.end(); ++it) {
            auto value = it.value().toObject();
            value["symbol"] = it.key();
            provisional(value);
        }
        persistMatchingState();
        if (candidate.value("requiredComplete").toBool()) {
            const auto draft = m_run + "/draft-pack.json";
            writeObject(draft, candidate.value("pack").toObject());
            validate(draft, true);
        } else {
            Cache(m_root).candidate(m_run, m_fingerprint, "waiting-for-runtime-change");
            scheduleRetry(
                m_reference.valid()
                    ? "Unresolved symbols await relevant classes/evidence"
                    : "No verified reference selected; waiting for runtime/reference change");
        }
    });
}
