#include "../../mapping/cache/MappingCache.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <cstdio>
#include <stdexcept>
#include <windows.h>
using namespace mapping_cache;
void emitEvent(QJsonObject value) {
    value["eventVersion"] = 1;
    auto line = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 2)
        return 2;
    auto get = [&](QString key) {
        const int i = args.indexOf(key);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString{};
    };
    const auto stateFile = qEnvironmentVariable("ARCVEIL_DYNAMIC_FIXTURE_STATE");
    const auto logFile = qEnvironmentVariable("ARCVEIL_DYNAMIC_FIXTURE_LOG");
    auto state = QFile::exists(stateFile) ? readObject(stateFile) : QJsonObject{};
    auto log = [&](QString text) {
        QFile file(logFile);
        if (!file.open(QIODevice::Append))
            throw std::runtime_error("fixture log unavailable");
        file.write(text.toUtf8() + '\n');
    };
    const auto mode = qEnvironmentVariable("ARCVEIL_DYNAMIC_FIXTURE_MODE");
    const auto command = args[1];
    const QJsonObject identity{{"family", "Lunar"}, {"minecraftVersion", "1.8.9"},
        {"mappingIdentity", "fixture-target-identity"}, {"metadataIdentity", "fixture-target-metadata"}};
    if (command == "identify") { writeObject(get("--out"), identity); return 0; }
    if (command == "inspect") {
        if (mode == "capture-fatal") {
            emitEvent({{"event", "failure"}, {"reason", "fixture capture unavailable"}});
            return 1;
        }
        const bool bootstrapWarmup = !args.contains("--detect-only") && state.value("lite").toInt() > 0;
        const int index = state.value("lite").toInt() + (bootstrapWarmup ? 0 : 1);
        state["lite"] = index;
        QString phase = index <= 2 ? "A" : index == 3 ? "U" : index == 4 ? "C" : "D";
        if (state.value("forceE").toBool())
            phase = "E";
        writeObject(stateFile, state);
        writeObject(get("--out"), {{"phase", phase}});
        log("lite " + phase);
        const auto pid = get("--pid").toUInt();
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        FILETIME c{}, e{}, k{}, u{};
        if (!h || !GetProcessTimes(h, &c, &e, &k, &u))
            return 1;
        CloseHandle(h);
        const auto start = QString::number((quint64(c.dwHighDateTime) << 32) | c.dwLowDateTime);
        emitEvent({{"event", "fingerprint"},
                   {"fingerprint", "lite-" + phase},
                   {"pid", double(pid)},
                   {"processStart", start},
                   {"classes", 100}});
        return 0;
    }
    if (command == "select") {
        const auto phase = readObject(get("--snapshot")).value("phase").toString();
        writeObject(get("--out"),
                    {{"identity",identity},{"phase", phase},
                     {"relevantFingerprint", "relevant-" + (phase == "U" ? "A" : phase)},
                     {"classes", QJsonArray{QJsonObject{{"name", "Fixture"}}}}});
        log("select " + phase);
        log("select-pack " + get("--pack"));
        return 0;
    }
    if (command == "inspect-detail") {
        auto phase = readObject(get("--candidates")).value("phase").toString();
        if (mode == "drift" && state.value("validated").toBool() &&
            !state.value("drifted").toBool()) {
            phase = "E";
            state["drifted"] = true;
            state["forceE"] = true;
            writeObject(stateFile, state);
        }
        if (mode == "capture-drift" && !state.value("captureDrifted").toBool()) {
            state["captureDrifted"] = true;
            writeObject(stateFile, state);
            emitEvent({{"event", "failure"},
                       {"retryable", true},
                       {"failureType", "runtime-drift"},
                       {"reason", "fixture metadata drift"}});
            log("detail drift");
            return 5;
        }
        writeObject(get("--out"), {{"phase", phase}});
        log("detail " + phase);
        const auto pid = get("--pid").toUInt();
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        FILETIME c{}, e{}, k{}, u{};
        if (!h || !GetProcessTimes(h, &c, &e, &k, &u))
            return 1;
        CloseHandle(h);
        emitEvent({{"event", "fingerprint"},
                   {"fingerprint", "detail-" + phase},
                   {"pid", double(pid)},
                   {"processStart",
                    QString::number((quint64(c.dwHighDateTime) << 32) | c.dwLowDateTime)}});
        return 0;
    }
    if (command == "validate") {
        auto pack = readObject(get("--pack"));
        const bool valid = pack.value("dynamicDraft").toBool();
        auto phase = readObject(get("--snapshot")).value("phase").toString();
        log("validate " + phase + " " + (valid ? "success" : "incomplete"));
        QJsonArray symbols;
        if (valid) {
            const auto values = pack.value("providers")
                                    .toArray()[0]
                                    .toObject()
                                    .value("dictionaries")
                                    .toArray()[0]
                                    .toObject()
                                    .value("symbols")
                                    .toObject();
            for (auto it = values.begin(); it != values.end(); ++it)
                symbols.append(QJsonObject{{"symbol", it.key()},
                                           {"accepted", true},
                                           {"mapping", it.value()},
                                           {"confidence", 1.0}});
            if (mode == "validation-reject" && phase == "D") {
                auto row = symbols[99].toObject();
                row["accepted"] = false;
                symbols[99] = row;
                state["forceE"] = true;
                log("validation rejected automatic binding");
            }
            state["validated"] = true;
            writeObject(stateFile, state);
        }
        writeObject(get("--out"), {{"identity",identity},{"valid", valid},
                                   {"injectionReady", valid},
                                   {"fingerprint", "detail-" + phase},
                                   {"symbols", symbols},
                                   {"pack", pack},
                                   {"attempts", QJsonArray{}}});
        emitEvent({{"event", "validation"}, {"valid", valid}});
        return valid ? 0 : 3;
    }
    if (command == "resolve") {
        auto phase = readObject(get("--snapshot")).value("phase").toString();
        QJsonObject previous, accepted;
        if (!get("--state").isEmpty())
            previous = readObject(get("--state"));
        accepted = previous.value("accepted").toObject();
        const int available = phase == "A" || phase == "U" ? 70 : phase == "C" ? 90 : 100;
        int started = 0, revalidated = 0;
        QJsonArray unresolved, results;
        QJsonObject values;
        for (int i = 0; i < 100; ++i) {
            const auto key = QString("symbol%1").arg(i, 3, 10, QChar('0'));
            const bool changed =
                phase == "E" && mode != "validation-reject" && i == 0 &&
                accepted.value(key).toObject().value("mapping") != "runtime.changed0";
            if (accepted.contains(key) && !changed) {
                auto result = accepted.value(key).toObject();
                result["event"] = "symbol-revalidated";
                emitEvent(result);
                ++revalidated;
            } else {
                if (changed) {
                    accepted.remove(key);
                    emitEvent({{"event", "symbol-invalidated"},
                               {"symbol", key},
                               {"reason", "fixture affected installed bytes"}});
                }
                ++started;
                emitEvent({{"event", "symbol-started"}, {"symbol", key}});
                const bool ok = i < available;
                const QString value = phase == "E" && mode != "validation-reject" && i == 0
                                          ? "runtime.changed0"
                                          : QString("runtime.Type%1").arg(i);
                QJsonObject result = {{"event", "symbol"},
                                      {"symbol", key},
                                      {"accepted", ok},
                                      {"mapping", ok ? QJsonValue(value) : QJsonValue()},
                                      {"confidence", ok ? 0.99 : 0.0},
                                      {"threshold", 0.98},
                                      {"margin", ok ? 1.0 : 0.0},
                                      {"evidence", QJsonArray{"synthetic unique evidence"}},
                                      {"bindingProof", "fixture-proof-" + value},
                                      {"provisional", ok}};
                emitEvent(result);
                if (ok)
                    accepted[key] = result;
                else
                    unresolved.append(key);
            }
            if (accepted.contains(key)) {
                const auto row = accepted.value(key).toObject();
                values[key] = row.value("mapping");
                results.append(row);
            } else
                values[key] = "";
        }
        log(QString("resolve %1 started=%2 revalidated=%3 matched=%4")
                .arg(phase)
                .arg(started)
                .arg(revalidated)
                .arg(accepted.size()));
        auto pack = readObject(get("--pack"));
        auto provider = pack.value("providers").toArray()[0].toObject();
        auto dict = provider.value("dictionaries").toArray()[0].toObject();
        dict["symbols"] = values;
        provider["dictionaries"] = QJsonArray{dict};
        pack["providers"] = QJsonArray{provider};
        pack["dynamicDraft"] = true;
        const bool complete = accepted.size() == 100;
        writeObject(get("--out"), {{"stateVersion", 1},
                                   {"requiredComplete", complete},
                                   {"complete", complete},
                                   {"fingerprint", "detail-" + phase},
                                   {"accepted", accepted},
                                   {"unresolved", unresolved},
                                   {"symbols", results},
                                   {"pack", pack}});
        return complete ? 0 : 4;
    }
    return 2;
}
