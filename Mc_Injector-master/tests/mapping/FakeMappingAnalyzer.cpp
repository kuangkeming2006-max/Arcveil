#include "../../mapping/cache/MappingCache.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QThread>
#include <cstdio>
#include <windows.h>
using namespace mapping_cache;
void event(QJsonObject o) {
    o["eventVersion"] = 1;
    auto s = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments();
    if (args.size() < 2)
        return 2;
    auto get = [&](QString key) {
        int i = args.indexOf(key);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString{};
    };
    const auto mode = qEnvironmentVariable("ARCVEIL_MAPPING_TEST_MODE");
    QFile log(qEnvironmentVariable("ARCVEIL_MAPPING_TEST_LOG"));
    log.open(QIODevice::Append);
    log.write(args[1].toUtf8() + '\n');
    log.close();
    if (mode == "oversized-request" && args[1] == "inspect") {
        event({{"event", "failure"}, {"limitName", "probe-request-json"},
               {"reason", "snapshot size limit: layer=file limit=probe-request-json observed=45913 configured=32768"}});
        return 1;
    }
    if (mode == "slow")
        QThread::msleep(5000);
    if (mode == "flood")
        for (int i = 0; i < 1000; ++i)
            event({{"event", "symbol"}, {"symbol", QString::number(i)}, {"confidence", 0.99}});
    const QJsonObject identity{{"family", mode.startsWith("vanilla") ? "Vanilla" : mode == "unknown-family" ? "Unknown" : "Forge"},
        {"minecraftVersion", mode == "modern-forge" ? "1.20.1" : "1.8.9"},
        {"versionVerified", mode != "unverified-version"}, {"selectedPack", get("--pack")},
        {"mappingNamespace", "Notch"}, {"detectionReason", "explicit Mock identity fixture"},
        {"mappingIdentity", "fixture-mapping-identity"}, {"metadataIdentity", "fixture-metadata-identity"},
        {"bindingIdentity", QString(64, QLatin1Char('a'))},
        {"runtimeBinding", QJsonObject{{"anchor", "net.minecraft.client.Minecraft"}, {"type", "fixture.Loader"}, {"instance", 1}, {"loaderKey", "fixture-loader"}}}};
    if (args[1] == "identify" || args[1] == "identity-proof") {
        auto result=identity;
        if(mode.startsWith("recover-version")) {
            const bool recovered=get("--snapshot").endsWith("candidate-version-evidence.jsonl");
            if(!recovered || mode=="recover-version-missing") {
                result["versionVerified"]=false;result["minecraftVersion"]="";
                result["evidenceRecovery"]=QJsonObject{{"eligible",true},{"family","Forge"},
                    {"versions",QJsonArray{"1.8.9"}},{"runtimeBinding",identity.value("runtimeBinding")}};
            } else if(mode=="recover-version-wrong-loader") {
                auto binding=result.value("runtimeBinding").toObject();binding["instance"]=2;result["runtimeBinding"]=binding;
            } else if(mode=="recover-version-wrong-family")result["family"]="Lunar";
            else if(mode=="recover-version-wrong-version")result["minecraftVersion"]="1.20.1";
        }
        writeObject(get("--out"), result); return 0;
    }
    if(args[1]=="select") {
        const auto snapshot=readObject(get("--snapshot"));
        writeObject(get("--out"),{{"identity", identity},{"relevantFingerprint",snapshot.value("fingerprint")},{"classes",QJsonArray{QJsonObject{{"name","Fixture"}}}}});
        return 0;
    }
    if (args[1] == "inspect" || args[1]=="inspect-detail") {
        auto pid = get("--pid").toUInt();
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        FILETIME c{}, e{}, k{}, u{};
        if (!h || !GetProcessTimes(h, &c, &e, &k, &u))
            return 1;
        CloseHandle(h);
        QString fp = args[1]=="inspect"?"fixture-lite":"fixture-fingerprint";
        if (mode == "changed" && get("--out").endsWith("authored-metadata-3.jsonl"))
            fp = "unrelated-metadata-fingerprint";
        const auto tier=get("--capture-tier");
        if(mode=="empty-authored-candidate" && tier=="modded-validation" && get("--pack").endsWith("authored-candidate-0.json")) {
            event({{"event","failure"},{"failureType","candidate-scope-unavailable"},{"reason","Mock selected names absent in pinned loader"},
                   {"pid",double(pid)},{"processStart",QString::number((quint64(c.dwHighDateTime)<<32)|c.dwLowDateTime)}});
            return 6;
        }
        int dictionaryCount=0;
        if(!get("--pack").isEmpty()) for(const auto &provider:readObject(get("--pack")).value("providers").toArray())
            dictionaryCount+=provider.toObject().value("dictionaries").toArray().size();
        event({{"event","CAPTURE_REQUEST"},{"captureTier",tier},{"scopedDictionaryCount",dictionaryCount},
               {"evidenceKind","Mock capture transport; no actual JVMTI request"}});
        writeObject(get("--out"), {{"testSnapshot", true},{"fingerprint",fp}});
        event({{"event", "fingerprint"},
               {"fingerprint", fp},
               {"pid", double(pid)},
               {"processStart",
                QString::number((quint64(c.dwHighDateTime) << 32) | c.dwLowDateTime)}});
        return 0;
    }
    if (args[1] == "validate" || args[1] == "validate-cache" || args[1] == "validate-authored") {
        if (mode == "unresolved" || mode == "vanilla-mismatch" ||
            (mode == "vanilla-final-mismatch" && get("--out").endsWith("authored-final.json")) ||
            (mode == "changed" && args[1] == "validate-cache")) {
            writeObject(get("--out"), {{"valid", false}});
            event({{"event", "validation"}, {"valid", false}});
            return 3;
        }
        auto pack = readObject(get("--pack"));
        auto provider=pack.value("providers").toArray().first().toObject();
        for(const auto& candidate:pack.value("providers").toArray())
            if(candidate.toObject().value("family")==identity.value("family")) {
                provider=candidate.toObject();break;
            }
        auto dictionary=provider.value("dictionaries").toArray().first().toObject();
        provider["dictionaries"]=QJsonArray{dictionary};pack["providers"]=QJsonArray{provider};
        const auto contracts=readObject(get("--contracts")).value("symbols").toObject();
        const auto values=dictionary.value("symbols").toObject();
        QJsonArray symbols;
        for(auto it=contracts.begin();it!=contracts.end();++it) if(it.value().toObject().value("required").toBool())
            symbols.append(QJsonObject{{"symbol",it.key()},{"accepted",true},{"mapping",values.value(it.key())},{"confidence",1.0}});
        writeObject(get("--out"), {{"identity",identity},{"valid", true},
                                   {"injectionReady", true},
                                   {"validationMode", args[1] == "validate-authored" ? "authored-metadata" : "installed-runtime"},
                                   {"selectedPack", pack},
                                   {"symbols", symbols},
                                   {"fingerprint", readObject(get("--snapshot")).value("fingerprint")},
                                   {"pack", pack}});
        event({{"event", "validation"}, {"valid", true}});
        return 0;
    }
    if (args[1] == "resolve") {
        QJsonArray unresolved;const auto specs=readObject(get("--contracts")).value("symbols").toObject();
        for(auto it=specs.begin();it!=specs.end();++it)unresolved.append(it.key());
        writeObject(get("--out"), {{"stateVersion",1},{"complete", false},{"requiredComplete",mode == "changed"},
            {"accepted",QJsonObject{}},{"unresolved",unresolved},{"pack",readObject(get("--pack"))},
            {"fingerprint",readObject(get("--snapshot")).value("fingerprint")}});
        event({{"event", "candidate"}, {"complete", false}});
        return mode == "changed" ? 0 : 4;
    }
    return 2;
}
