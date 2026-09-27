#include "../../mapping/cache/MappingCache.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
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
    if (args[1] == "inspect") {
        auto pid = get("--pid").toUInt();
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        FILETIME c{}, e{}, k{}, u{};
        if (!h || !GetProcessTimes(h, &c, &e, &k, &u))
            return 1;
        CloseHandle(h);
        QString fp = "fixture-fingerprint";
        if (mode == "changed" && get("--out").endsWith("final-snapshot.json"))
            fp = "changed-fingerprint";
        writeObject(get("--out"), {{"testSnapshot", true}});
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
        writeObject(get("--out"), {{"valid", true},
                                   {"injectionReady", true},
                                   {"fingerprint", "fixture-fingerprint"},
                                   {"pack", pack}});
        event({{"event", "validation"}, {"valid", true}});
        return 0;
    }
    if (args[1] == "resolve") {
        writeObject(get("--out"), {{"complete", false}, {"fingerprint", "fixture-fingerprint"}});
        event({{"event", "candidate"}, {"complete", false}});
        return 4;
    }
    return 2;
}
