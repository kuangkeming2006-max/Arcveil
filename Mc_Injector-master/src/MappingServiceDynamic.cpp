#include "MappingService.h"
#include <QFile>
#include <QDir>
using namespace mapping_cache;

void MappingService::stopMatching() {
    if (!m_busy || !current()) return;
    m_watchEnabled = false;
    m_watchTimer.stop();
    m_transaction->state = AttachTransaction::State::MappingPaused;
    m_status = "Attach waiting for mapping; resume to continue";
    // In-flight capture/resolve/final validation owns its current generation.
    // Stopping watch neither cancels that job nor changes OverlayManager's state.
    event({{"event", "snapshot-watch"},
           {"state", "paused"},
           {"reason", "User stopped snapshot watch/retry"}});
    emit changed();
}
void MappingService::resumeMatching() {
    if (!m_busy || !current() || m_watchEnabled)
        return;
    const auto generation = m_generation;
    m_watchEnabled = true;
    m_stableAmbiguity = false;
    m_transaction->state = AttachTransaction::State::MappingResolving;
    m_pendingChange = true;
    event({{"event", "snapshot-watch"}, {"state", "running"}});
    if (generation != m_generation || !current()) return;
    emit changed();
    if (generation != m_generation || !current()) return;
    if (!m_process) {
        disconnect(&m_watchTimer, nullptr, this, nullptr);
        connect(&m_watchTimer, &QTimer::timeout, this, [this, generation] {
            if (generation == m_generation && current()) watchLite();
        });
        m_watchTimer.start(m_watchInterval);
    }
}
void MappingService::scheduleRetry(const QString &reason, bool unchanged) {
    if (!m_busy || !current())
        return;
    const auto generation = m_generation;
    m_status =
        m_watchEnabled ? "Attach waiting for runtime classes" : "Attach waiting for mapping; resume to continue";
    event({{"event", "waiting-for-runtime-change"},
           {"reason", reason},
           {"unresolved", QJsonArray::fromStringList(m_unresolved)},
           {"matched", m_provisional.size()},
           {"paused", !m_watchEnabled}});
    if (generation != m_generation || !current()) return;
    if (m_watchEnabled && !m_stableAmbiguity) {
        event({{"event", "retry-scheduled"},
               {"delayMs", m_watchInterval},
               {"generation", double(m_snapshotGeneration)}});
        if (generation != m_generation || !current()) return;
        disconnect(&m_watchTimer, nullptr, this, nullptr);
        connect(&m_watchTimer, &QTimer::timeout, this, [this, generation] {
            if (generation == m_generation && current()) watchLite();
        });
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
    captureLite(m_family.isEmpty() ? CapturePhase::Initial : CapturePhase::Watch);
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
    m_finalCheck = phase == CapturePhase::Final;
    if (!identityValid()) {
        fail("Target JVM exited or changed identity");
        return;
    }
    m_capture = {};
    ++m_captureSerial;
    m_litePath = m_run + QString("/index-%1.jsonl").arg(m_captureSerial);
    if (phase == CapturePhase::Initial) {
        m_scopePack = m_defaultPack;
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
    {
        args << "--pack" << (phase == CapturePhase::CacheWarmup ? m_hit.pack : m_defaultPack) << "--contracts" << m_contracts;
        if (phase == CapturePhase::CacheWarmup) args << "--required-only";
        else if (phase != CapturePhase::Initial || (!m_fullLite && !m_authoredWarmup)) args << "--detect-only";
        if (!m_fullLite) args << "--identity-lite" << "--identity-packs" << m_identityPacksFile;
    }
    if (m_transport == "NativeLoader") args << "--transport" << "native";
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
            const auto reference = Cache(m_root).reference(m_contractDigest, m_family, m_minecraftVersion, m_mappingIdentity);
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
        if (phase == CapturePhase::Initial) identifyTarget();
        else selectDetail(phase == CapturePhase::CacheWarmup ? CapturePhase::Initial : phase);
    });
}
void MappingService::selectDetail(CapturePhase phase) {
    m_selectionPath = m_run + QString("/selection-%1.json").arg(m_captureSerial);
    // Preserve authored-pack bootstrap across client families. The reference pack
    // describes source names; it must not hide classes available in the target pack.
    const auto selectionPack = m_cachePath ? m_hit.pack : (m_dynamic ? m_scopePack : m_defaultPack);
    QStringList args = {"select",   "--allow-empty", "--pack",    selectionPack, "--snapshot",
                        m_litePath, "--contracts",   m_contracts, "--out",       m_selectionPath};
    if (m_cachePath) args << "--validation-scope";
    else if (m_reference.valid())
        args << "--reference" << m_reference.snapshot;
    if (phase == CapturePhase::Watch && m_dynamic && !m_snapshot.isEmpty())
        args << "--reuse-snapshot" << m_snapshot;
    launch(args, [this, phase](int code) {
        if (code) {
            fail("Analyzer cannot select detail scope: " +
                 m_analyzerFailure.value("reason").toString());
            return;
        }
        const auto selection = readObject(m_selectionPath);
        if (m_cachePath && phase == CapturePhase::Initial) {
            const auto detected = selection.value("identity").toObject().value("metadataIdentity").toString();
            if (detected.isEmpty() || detected != m_hit.metadataIdentity) {
                if (!selection.value("identity").toObject().value("identityComplete").toBool(true) && !m_candidateWarmed) {
                    m_candidateWarmed = true;
                    captureLite(CapturePhase::CacheWarmup);
                    return;
                }
                event({{"event", "CACHE_LOOKUP"}, {"hit", false}, {"cacheKey", m_hit.mappingIdentity},
                       {"mappingIdentity", m_hit.mappingIdentity}, {"reason", "required-class-metadata-changed"}});
                ++m_candidateIndex;
                m_candidateWarmed = false;
                selectCacheCandidate();
                return;
            }
            event({{"event", "CACHE_LOOKUP"}, {"hit", true}, {"cacheKey", m_hit.mappingIdentity},
                   {"mappingIdentity", m_hit.mappingIdentity}, {"reason", "stable-metadata-candidate; live-validation-required"}});
            m_status = "Cache HIT -> Live validation";
            emit changed();
        }
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
        if (selection.value("classes").toArray().isEmpty() && selection.value("reuseClasses").toArray().isEmpty()) {
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
                if (operation != m_generation || serial != m_captureSerial || !m_busy || !current())
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
    if (m_transport == "NativeLoader") args << "--transport" << "native";
    if (!m_modular)
        args << "--tools-jar" << m_toolsJar;
    if (phase == CapturePhase::Watch && m_dynamic && !m_snapshot.isEmpty())
        args << "--reuse-snapshot" << m_snapshot;
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
            if (m_cachePath) { fallbackFromCache("final-runtime-fingerprint-drift"); return; }
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
void MappingService::identifyTarget() {
    launch({"identify", "--pack", m_defaultPack, "--snapshot", m_litePath,
            "--contracts", m_contracts, "--out", m_run + "/identity.json"}, [this](int code) {
        if (code) { fail("Cannot identify target family"); return; }
        const auto identity = readObject(m_run + "/identity.json");
        m_family = identity.value("family").toString();
        m_minecraftVersion = identity.value("minecraftVersion").toString();
        m_candidates = m_forceAutomatic ? QList<Entry>{} : Cache(m_root).candidates(m_family, m_minecraftVersion, m_contractDigest);
        m_candidateIndex = 0;
        selectCacheCandidate();
    });
}
void MappingService::selectCacheCandidate() {
    if (!m_busy || !current()) return;
    if (m_candidateIndex < m_candidates.size()) {
        m_hit = m_candidates.at(m_candidateIndex);
        checkCached(m_hit);
        return;
    }
    m_hit = {}; m_cachePath = false;
    event({{"event", "CACHE_LOOKUP"}, {"hit", false}, {"cacheKey", m_mappingIdentity},
           {"mappingIdentity", m_mappingIdentity}, {"targetFamily", m_family},
           {"reason", m_candidates.isEmpty() ? "no-verified-family-candidate" : "no-compatible-stable-metadata"}});
    m_reference = Cache(m_root).reference(m_contractDigest, m_family, m_minecraftVersion, m_mappingIdentity);
    event({{"event", "REFERENCE_SELECTION"}, {"sourceFamily", m_reference.family},
           {"targetFamily", m_family}, {"sourceMappingIdentity", m_reference.mappingIdentity},
           {"targetMappingIdentity", m_mappingIdentity},
           {"reason", m_reference.valid() ? m_reference.referenceReason : "no-family-compatible-reference"}});
    m_scopePack = m_reference.valid() ? m_reference.pack : m_defaultPack;
    progressReference(m_reference);
    selectDetail(CapturePhase::Initial);
}
void MappingService::checkCached(const Entry &entry) {
    if (!m_busy || !identityValid()) return;
    const auto generation = m_generation;
    if (entry.bindingIdentity.isEmpty()) {
        // PR #6 already persisted a verified full source. Derive the new compact
        // proof offline once; repeating live detail would add no evidence.
        auto source = Cache(m_root).lookup(entry.mappingIdentity, m_contractDigest);
        if (!source.valid()) { fallbackFromCache("legacy-verified-source-invalid"); return; }
        launch({"identify", "--pack", source.pack, "--snapshot", source.snapshot, "--contracts", m_contracts,
                "--out", m_run + "/legacy-binding-proof.json"}, [this, source](int code) mutable {
            if (code) { fallbackFromCache("legacy-binding-proof-unavailable"); return; }
            const auto identity = readObject(m_run + "/legacy-binding-proof.json");
            if (identity.value("mappingIdentity").toString() != source.mappingIdentity ||
                identity.value("metadataIdentity").toString() != source.metadataIdentity) {
                fallbackFromCache("legacy-verified-source-identity-mismatch"); return;
            }
            source.bindingIdentity = identity.value("bindingIdentity").toString();
            if (source.bindingIdentity.size() != 64) { fallbackFromCache("legacy-binding-proof-incomplete"); return; }
            m_upgradeCacheProof = true; m_snapshot = source.snapshot;
            checkCached(source);
        });
        return;
    }
    m_hit = entry; m_reference = entry; m_cachePath = true;
    m_family = entry.family; m_minecraftVersion = entry.minecraftVersion;
    m_status = "Checking cached required classes and members";
    emit changed();
    if (generation != m_generation || !m_busy || !current()) return;
    m_litePath = m_run + "/required-bindings.jsonl";
    QStringList args = {"inspect", "--lite", "--binding-check", "--identity-lite", "--required-only",
        "--pid", QString::number(m_pid), "--java", m_java, "--helper", m_helper,
        "--probe", m_probe, "--native-loader", m_tools + "/McOverlayNativeLoader.exe",
        "--pack", entry.pack, "--contracts", m_contracts, "--out", m_litePath};
    if (m_transport == "NativeLoader") args << "--transport" << "native";
    if (!m_modular) args << "--tools-jar" << m_toolsJar;
    m_capture = {};
    launch(args, [this, generation](int code) {
        if (code) { fallbackFromCache("required-binding-capture-failed"); return; }
        if (!identityValid() || m_capture.value("pid").toDouble() != m_pid ||
            m_capture.value("processStart").toString() != m_start) { fail("Cached binding process identity mismatch"); return; }
        m_fingerprint = m_capture.value("fingerprint").toString();
        if (m_fingerprint.isEmpty()) { fail("Cached binding capture omitted fingerprint"); return; }
        launch({"validate-cache", "--pack", m_hit.pack, "--snapshot", m_litePath, "--contracts", m_contracts,
                "--binding-identity", m_hit.bindingIdentity,
                "--out", m_run + "/validation.json"}, [this, generation](int result) {
            if (result) { fallbackFromCache("cached-required-binding-validation-failed"); return; }
            m_validation = readObject(m_run + "/validation.json");
            const auto identity = m_validation.value("identity").toObject();
            if (!m_validation.value("valid").toBool() || !m_validation.value("injectionReady").toBool() ||
                m_validation.value("fingerprint").toString() != m_fingerprint ||
                identity.value("family").toString() != m_hit.family ||
                identity.value("bindingIdentity").toString() != m_hit.bindingIdentity) {
                fallbackFromCache("cached-required-binding-receipt-mismatch"); return;
            }
            m_mappingIdentity = m_hit.mappingIdentity; m_metadataIdentity = m_hit.metadataIdentity;
            m_runtimeBinding = identity.value("runtimeBinding").toObject();
            m_pack = m_hit.pack;
            event({{"event", "CACHE_LOOKUP"}, {"hit", true}, {"cacheKey", m_mappingIdentity},
                   {"mappingIdentity", m_mappingIdentity}, {"reason", "required-members-and-installed-digests-verified"}});
            if (generation != m_generation || !current()) return;
            progressReference(m_hit);
            if (generation != m_generation || !current()) return;
            QElapsedTimer finalElapsed; finalElapsed.start();
            if (!identityValid() || fileDigest(m_hit.pack, 2 * 1024 * 1024) != m_hit.digest) {
                fail("Target or verified pack changed before Agent loading"); return;
            }
            performance("final check", finalElapsed.elapsed());
            if (generation == m_generation && current()) finalize();
        });
    });
}
void MappingService::fallbackFromCache(const QString &reason) {
    const auto generation = m_generation;
    event({{"event", "CACHE_LOOKUP"}, {"hit", false}, {"cacheKey", m_hit.mappingIdentity},
           {"mappingIdentity", m_hit.mappingIdentity}, {"reason", reason}});
    if (generation != m_generation || !current() || !m_busy) return;
    m_mappingIdentity.clear(); // Cached identity failed live evidence; target identity is unknown.
    m_candidateIndex = m_candidates.size();
    m_forceAutomatic = true;
    m_hit = {}; m_cachePath = false; m_validation = {};
    m_runtimeBinding = {};
    m_upgradeCacheProof = false;
    m_identityPacksFile = m_run + "/identity-packs.json";
    writeObject(m_identityPacksFile, {{"packs", QJsonArray::fromStringList(Cache(m_root).knownPacks(m_contractDigest))}});
    // Recapture the structural candidate scope; cached validation scope is too narrow
    // to supply incoming-reference evidence to automatic matching.
    m_fullLite = true;
    captureLite(CapturePhase::Initial);
}
void MappingService::choosePack() {
    if (m_forceAutomatic && !m_cachePath) resolve();
    else validate(m_cachePath ? m_hit.pack : m_defaultPack, false);
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
                       if (m_cachePath) { fallbackFromCache("cached-pack-live-validation-failed"); return; }
                       if (!m_authoredWarmup) { m_authoredWarmup = true; captureLite(CapturePhase::Initial); return; }
                       if (m_reference.valid() && !m_fullLite) { m_fullLite = true; captureLite(CapturePhase::Initial); return; }
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
               const auto identity = m_validation.value("identity").toObject();
               const auto stableIdentity = identity.value("mappingIdentity").toString();
               if (m_cachePath && (stableIdentity.isEmpty() || stableIdentity != m_hit.mappingIdentity)) {
                   fallbackFromCache("cached-installed-class-structure-changed"); return;
               }
               m_mappingIdentity = stableIdentity;
               m_metadataIdentity = identity.value("metadataIdentity").toString();
               m_runtimeBinding = identity.value("runtimeBinding").toObject();
               // Upgrade legacy verified objects atomically after the normal full
               // validation/final recheck, so future attaches can use compact proof.
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
    if (!m_busy || !current()) return;
    m_dynamic = true;
    m_transaction->state = m_watchEnabled ? AttachTransaction::State::MappingResolving : AttachTransaction::State::MappingPaused;
    m_status = "Automatic structural resolve";
    event({{"event", "AUTO_RESOLVE"}, {"mappingIdentity", m_mappingIdentity}});
    emit changed();
    event({{"event", "step"},
           {"index", 2},
           {"state", "running"},
           {"message", "Matching unresolved / affected symbols; retaining revalidated bindings"}});
    progressReference(m_reference);
    QStringList args = {
        "resolve",  "--incremental", "--pack",    m_scopePack, "--snapshot",
        m_snapshot, "--contracts",   m_contracts, "--out",     m_run + "/candidate.json"};
    if (m_reference.valid()) args << "--reference" << m_reference.snapshot;
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
            const auto reasons = candidate.value("unresolvedReasons").toObject();
            bool missing = reasons.isEmpty();
            for (auto it = reasons.begin(); it != reasons.end(); ++it)
                missing |= it.value() == "missing-runtime-class";
            if (!missing) {
                m_stableAmbiguity = true;
                m_watchEnabled = false;
                m_watchTimer.stop();
                m_transaction->state = AttachTransaction::State::MappingPaused;
                event({{"event", "STABLE_AMBIGUITY"}, {"reason", "Stable evidence is ambiguous; automatic polling paused"}});
            }
            scheduleRetry(
                m_reference.valid()
                    ? "Unresolved symbols await relevant classes/evidence"
                    : "No verified reference selected; waiting for runtime/reference change");
        }
    });
}
