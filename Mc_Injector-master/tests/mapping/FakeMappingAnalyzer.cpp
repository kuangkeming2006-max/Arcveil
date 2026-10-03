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
    if (mode == "slow")
        QThread::msleep(5000);
    if (mode == "flood")
        for (int i = 0; i < 1000; ++i)
            event({{"event", "symbol"}, {"symbol", QString::number(i)}, {"confidence", 0.99}});
    if(args[1]=="select") {
        const auto snapshot=readObject(get("--snapshot"));
        writeObject(get("--out"),{{"relevantFingerprint",snapshot.value("fingerprint")},{"classes",QJsonArray{QJsonObject{{"name","Fixture"}}}}});
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
        if (mode == "changed" && args[1]=="inspect-detail" && get("--candidates").endsWith("selection-2.json"))
            fp = "changed-fingerprint";
        writeObject(get("--out"), {{"testSnapshot", true},{"fingerprint",fp}});
        event({{"event", "fingerprint"},
               {"fingerprint", fp},
               {"pid", double(pid)},
               {"processStart",
                QString::number((quint64(c.dwHighDateTime) << 32) | c.dwLowDateTime)}});
        return 0;
    }
    if (args[1] == "validate") {
        if (mode == "unresolved") {
            writeObject(get("--out"), {{"valid", false}});
            event({{"event", "validation"}, {"valid", false}});
            return 3;
        }
        auto pack = readObject(get("--pack"));
        auto provider=pack.value("providers").toArray().first().toObject();
        auto dictionary=provider.value("dictionaries").toArray().first().toObject();
        provider["dictionaries"]=QJsonArray{dictionary};pack["providers"]=QJsonArray{provider};
        const auto contracts=readObject(get("--contracts")).value("symbols").toObject();
        const auto values=dictionary.value("symbols").toObject();
        QJsonArray symbols;
        for(auto it=contracts.begin();it!=contracts.end();++it) if(it.value().toObject().value("required").toBool())
            symbols.append(QJsonObject{{"symbol",it.key()},{"accepted",true},{"mapping",values.value(it.key())},{"confidence",1.0}});
        writeObject(get("--out"), {{"valid", true},
                                   {"injectionReady", true},
                                   {"symbols", symbols},
                                   {"fingerprint", readObject(get("--snapshot")).value("fingerprint")},
                                   {"pack", pack}});
        event({{"event", "validation"}, {"valid", true}});
        return 0;
    }
    if (args[1] == "resolve") {
        QJsonArray unresolved;const auto specs=readObject(get("--contracts")).value("symbols").toObject();
        for(auto it=specs.begin();it!=specs.end();++it)unresolved.append(it.key());
        writeObject(get("--out"), {{"stateVersion",1},{"complete", false},{"requiredComplete",false},
            {"accepted",QJsonObject{}},{"unresolved",unresolved},{"pack",readObject(get("--pack"))},
            {"fingerprint",readObject(get("--snapshot")).value("fingerprint")}});
        event({{"event", "candidate"}, {"complete", false}});
        return 4;
    }
    return 2;
}
