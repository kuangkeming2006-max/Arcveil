#include "MappingService.h"
#include <QJsonArray>
#include <QFileInfo>
using namespace mapping_cache;
namespace {
QJsonObject dictionary(const QJsonObject &pack) {
    const auto providers = pack.value("providers").toArray();
    if (providers.size() != 1)
        return {};
    const auto dictionaries = providers[0].toObject().value("dictionaries").toArray();
    return dictionaries.size() == 1 ? dictionaries[0].toObject() : QJsonObject{};
}
QString rendered(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    QStringList names;
    for (const auto &v : value.toArray())
        if (!v.toString().isEmpty())
            names << v.toString();
    return names.join(" | ");
}
QString logicalName(const QString &key, const QJsonObject &spec) {
    // Labels are presentation aliases; symbol identity remains the stable schema key.
    if (key == "playerField")
        return "Minecraft.thePlayer";
    auto owner = spec.value("owner").toString();
    if (owner.endsWith("Name"))
        owner.chop(4);
    if (!owner.isEmpty())
        owner[0] = owner[0].toUpper();
    return owner.isEmpty() ? key : owner + "." + key;
}
} // namespace
void MappingService::progressSchema() {
    m_progressContracts = {};
    m_progressSymbols = {};
    try {
        m_progressContracts = readObject(m_contracts).value("symbols").toObject();
        for (auto it = m_progressContracts.begin(); it != m_progressContracts.end(); ++it) {
            const auto spec = it.value().toObject();
            event({{"event", "symbol-queued"},
                   {"symbol", it.key()},
                   {"logicalName", logicalName(it.key(), spec)},
                   {"required", spec.value("required")}});
        }
    } catch (const std::exception &e) {
        event({{"event", "step"},
               {"index", 2},
               {"state", "degraded"},
               {"message", QString::fromUtf8(e.what())}});
    }
}
void MappingService::progressReference(const Entry &reference) {
    QString family = "Unknown";
    if (reference.valid()) {
        try {
            family = dictionary(readObject(reference.pack)).value("family").toString("Unknown");
        } catch (...) {
        } // Diagnostics cannot change the preflight result.
    }
    event({{"event", "reference"},
           {"referenceAvailable", reference.valid()},
           {"sourceClientFamily", family},
           {"sourceFingerprint", reference.fingerprint},
           {"sourcePack", reference.pack},
           {"sourceSnapshot", reference.snapshot},
           {"targetFingerprint", m_fingerprint},
           {"selectionReason",
            reference.valid()
                ? "Last verified reference with matching contract digest and artifact hashes"
                : "No reference selected"}});
}
void MappingService::progressAnalyzer(const QJsonObject &input) {
    const auto kind = input.value("event").toString();
    if (kind == "fingerprint") {
        QString family = "Unknown";
        int confidence = -1;
        for (const auto &v : input.value("launchEvidence").toArray()) {
            const auto e = v.toObject();
            const int score = e.value("confidence").toInt();
            if (score > confidence) {
                family = e.value("family").toString();
                confidence = score;
            } else if (score == confidence && family != e.value("family").toString())
                family = "Unknown";
        }
        event({{"event", "reference"},
               {"targetClientFamily", family},
               {"targetFingerprint", input.value("fingerprint")}});
    } else if (kind == "symbol" && m_progressContracts.contains(input.value("symbol").toString())) {
        auto e = input;
        // Analyzer 'accepted' belongs to a dictionary attempt, not the final validated pack.
        e["event"] = input.value("accepted").toBool() ? "symbol-progress" : "symbol-failed";
        e["runtimeName"] =
            rendered(input.value("runtimeMapping").isUndefined() ? input.value("mapping")
                                                                 : input.value("runtimeMapping"));
        if (input.value("accepted").toBool())
            e["reason"] = "Candidate evidence received; awaiting final validation";
        event(e);
        if (input.value("provisional").toBool() && input.value("accepted").toBool())
            provisional(input);
    } else if (kind == "symbol-invalidated") {
        invalidate(input.value("symbol").toString(), input.value("reason").toString());
    } else if (kind == "validation" && input.value("injectionReady").toBool()) {
        event({{"event", "reference"},
               {"targetClientFamily", dictionary(input.value("pack").toObject()).value("family")}});
    }
}
void MappingService::progressVerified() {
    // Called only after final fingerprint/identity recheck and successful cache promotion.
    // This optional display receipt cannot authorize an injection or modify a registry.
    try {
        const auto selected = dictionary(readObject(m_hit.pack));
        const auto values = selected.value("symbols").toObject();
        auto results = m_validation.value("symbols").toArray();
        const bool cache = results.isEmpty();
        if (cache) {
            const auto proof = readObject(QFileInfo(m_hit.pack).absolutePath() + "/proof.json");
            results = proof.value("symbols").toArray();
            if (results.isEmpty()) {
                // Older proofs validate required bindings but contain no optional-symbol receipt.
                for (auto it = m_progressContracts.begin(); it != m_progressContracts.end(); ++it)
                    if (it.value().toObject().value("required").toBool())
                        results.append(QJsonObject{
                            {"symbol", it.key()},
                            {"accepted", true},
                            {"mapping", values.value(it.key())},
                            {"confidence", 1.0},
                            {"evidence", QJsonArray{"Verified fingerprint cache; authored alias "
                                                    "set where applicable"}}});
            }
        }
        for (const auto &v : results) {
            const auto result = v.toObject();
            const auto key = result.value("symbol").toString();
            if (!result.value("accepted").toBool()) {
                auto e = result;
                e["event"] = "symbol-failed";
                event(e);
                continue;
            }
            const auto spec = m_progressContracts.value(key).toObject();
            auto runtime =
                rendered(result.contains("runtimeMapping") ? result.value("runtimeMapping")
                                                           : result.value("mapping"));
            const auto owner = values.value(spec.value("owner").toString()).toString();
            if (!owner.isEmpty() && !runtime.isEmpty())
                runtime = owner + "." + runtime;
            auto e = result;
            e["event"] = "symbol-matched";
            e["runtimeName"] = runtime;
            e["verified"] = true;
            if (m_progressSymbols.contains(key)) {
                const auto resolved = m_progressSymbols.value(key).toObject();
                e["confidence"] = resolved.value("confidence");
                e["evidence"] = resolved.value("evidence");
            }
            if (cache)
                e["source"] = "verified-cache";
            event(e);
        }
        event({{"event", "reference"}, {"targetClientFamily", selected.value("family")}});
        event({{"event", "complete"},
               {"scope", "mapping"},
               {"success", true},
               {"source", cache                         ? "verified-cache"
                          : m_progressSymbols.isEmpty() ? "live-validation"
                                                        : "automatic"}});
    } catch (const std::exception &e) {
        event({{"event", "step"},
               {"index", 3},
               {"state", "degraded"},
               {"message", QString("Mapping verified; display receipt unavailable: %1")
                               .arg(QString::fromUtf8(e.what()))}});
    }
}

void MappingService::provisional(QJsonObject value) {
    const auto symbol = value.value("symbol").toString();
    if (symbol.isEmpty() || m_provisional.contains(symbol))
        return;
    const auto runtime = rendered(value.value("mapping"));
    // Empty omissions are retained in state but have no actual runtime binding row.
    m_provisional[symbol] = value;
    if (runtime.isEmpty())
        return;
    auto owner = value.value("runtimeOwner").toString();
    value["runtimeName"] = owner.isEmpty() ? runtime : owner + "." + runtime;
    value["verified"] = false;
    value["provisional"] = true;
    value["reason"] = "provisional / awaiting-final-validation";
    value["event"] = "symbol-provisional-match";
    event(value);
    value["event"] = "symbol-matched";
    event(value);
}
