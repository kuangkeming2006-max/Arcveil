#include "../../src/MappingService.h"
#include "../../src/MappingProgressController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cstdio>
#include <stdexcept>
using namespace mapping_cache;
struct DynamicMappingServiceTests {
    static int run(const QString &worker) {
        int checks = 0, failures = 0;
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok) {
                ++failures;
                std::printf("FAIL %s\n", msg);
            }
        };
        QTemporaryDir temp;
        QJsonObject specs, values;
        for (int i = 0; i < 100; ++i) {
            const auto key = QString("symbol%1").arg(i, 3, 10, QChar('0'));
            specs[key] = QJsonObject{{"kind", "class"}, {"required", true}};
            values[key] = "source.Type" + QString::number(i);
        }
        const auto contracts = temp.path() + "/contracts.json", pack = temp.path() + "/pack.json",
                   reference = temp.path() + "/reference.json";
        writeObject(contracts, {{"contractVersion", 1},
                                {"symbols", specs},
                                {"requiredAlternatives", QJsonArray{}},
                                {"constructors", QJsonArray{}}});
        writeObject(pack, {{"providers",
                            QJsonArray{QJsonObject{
                                {"family", "Lunar"},
                                {"dictionaries", QJsonArray{QJsonObject{{"family", "Lunar"},
                                                                        {"id", "fixture"},
                                                                        {"symbols", values}}}}}}}});
        writeObject(reference, {{"fingerprint", "reference-fixture"}});
        auto spin = [&](std::function<bool()> done, int timeout = 15000) {
            QElapsedTimer timer;
            timer.start();
            while (!done() && timer.elapsed() < timeout)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            return done();
        };
        auto delay = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };
        auto readLog = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("fixture log unavailable");
            return file.readAll();
        };
        int scenario = 0;
        for (const auto &mode : QStringList{"normal", "drift", "validation-reject", "capture-drift",
                                            "stop", "cancel", "capture-fatal"}) {
            const auto dir = temp.path() + QString("/scenario-%1").arg(++scenario);
            QDir().mkpath(dir);
            qputenv("ARCVEIL_DYNAMIC_FIXTURE_STATE", (dir + "/worker.json").toUtf8());
            qputenv("ARCVEIL_DYNAMIC_FIXTURE_LOG", (dir + "/calls.log").toUtf8());
            qputenv("ARCVEIL_DYNAMIC_FIXTURE_MODE",
                    mode == "stop" || mode == "cancel" ? QByteArray("normal") : mode.toUtf8());
            MappingService service;
            service.m_root = dir + "/cache";
            service.m_analyzer = worker;
            service.m_contracts = contracts;
            service.m_defaultPack = pack;
            service.m_watchInterval = 20;
            service.m_maxWatchInterval = 60;
            service.m_debounceMs = 10;
            Cache(service.m_root)
                .promote(pack, reference, "reference-fixture", fileDigest(contracts),
                         {{"valid", true},
                          {"injectionReady", true},
                          {"fingerprint", "reference-fixture"}});
            MappingProgressController progress;
            progress.begin(quint32(QCoreApplication::applicationPid()));
            QObject::connect(&service, &MappingService::eventReceived, &progress,
                             &MappingProgressController::consume);
            QObject::connect(&progress, &MappingProgressController::stopMatchingRequested, &service,
                             &MappingService::stopMatching);
            QObject::connect(&progress, &MappingProgressController::resumeMatchingRequested,
                             &service, &MappingService::resumeMatching);
            int ready = 0, failed = 0, watchEvents = 0, drift = 0, invalidated = 0;
            bool paused = false, premature = false;
            QObject::connect(&service, &MappingService::ready,
                             [&](const auto &, const auto &) { ++ready; });
            QObject::connect(&service, &MappingService::failed, [&](const auto &) { ++failed; });
            QObject::connect(&service, &MappingService::eventReceived, [&](const QJsonObject &e) {
                const auto event = e.value("event").toString();
                watchEvents += event == "snapshot-watch";
                invalidated += event == "symbol-invalidated";
                drift +=
                    event == "snapshot-changed" && e.value("reason") == "final-fingerprint-drift";
                if (event == "symbol-matched" && e.value("verified").toBool() && service.busy())
                    premature = true;
                if (event == "waiting-for-runtime-change" && !paused &&
                    (mode == "stop" || mode == "cancel")) {
                    paused = true;
                    if (mode == "stop")
                        progress.stopMatching();
                    else
                        service.cancel();
                }
            });
            service.prepare(quint32(QCoreApplication::applicationPid()), "fixture-java",
                            "fixture-helper", true, {});
            if (mode == "stop") {
                check(spin([&] { return paused; }), "stop reaches partial matching state");
                const auto calls = readLog(dir + "/calls.log");
                delay(850);
                check(readLog(dir + "/calls.log") == calls,
                      "stop halts real future lite/detail/resolve jobs");
                check(service.busy() && !service.matchingEnabled() && !ready && !failed,
                      "stop preserves attach state and has no ready/failure");
                check(progress.completedCount() == 70 && !progress.successful(),
                      "70 provisional rows completed without authorizing success");
                auto state = readObject(service.m_statePath);
                check(state.value("accepted").toObject().size() == 70 &&
                          service.m_unresolved.size() == 30,
                      "service persists 70 matched and 30 unresolved");
                progress.resumeMatching();
            } else if (mode == "cancel") {
                check(spin([&] { return paused; }), "explicit attach cancel reached");
                const auto calls = readLog(dir + "/calls.log");
                delay(160);
                check(!service.busy() && !ready && !failed && readLog(dir + "/calls.log") == calls,
                      "cancel invalidates timer/jobs without stale ready");
                continue;
            }
            check(spin([&] { return ready || failed; }),
                  "dynamic lifecycle terminates when ready/fatal");
            if (mode == "capture-fatal") {
                check(failed == 1 && !ready,
                      "unavailable capture becomes fatal after bounded recovery");
                continue;
            }
            check(ready == 1 && !failed && !premature,
                  "only full validation and final recheck emit ready");
            const auto calls = readLog(dir + "/calls.log");
            check(calls.indexOf(("select-pack " + pack).toUtf8()) >= 0,
                  "authored bootstrap selects default target pack even with another reference");
            check(calls.contains("resolve A started=100 revalidated=0 matched=70"),
                  "snapshot A matches 70/100");
            check(calls.contains("resolve C started=30 revalidated=70 matched=90"),
                  "snapshot C retries only 30 and adds 20");
            check(calls.contains("resolve D started=10 revalidated=90 matched=100"),
                  "snapshot D retries final 10");
            check(!calls.contains("detail U") && calls.count("detail A") == 1,
                  "unchanged and irrelevant lite updates skip detail");
            check(calls.indexOf("validate D success") > calls.indexOf("resolve D"),
                  "full final validation follows required completion");
            check(watchEvents > 0 && !service.matchingEnabled(),
                  "real watch lifecycle ends at verified completion");
            check(
                Cache(service.m_root).lookup(service.fingerprint(), fileDigest(contracts)).valid(),
                "only final verified generation promoted to cache");
            if (mode == "stop") {
                bool stable = false;
                const auto completed = progress.completed();
                for (int i = 0; i < completed->rowCount(); ++i) {
                    const auto row = completed->index(i, 0);
                    if (completed->data(row, MappingProgressModel::Symbol) == "symbol000")
                        stable = completed->data(row, MappingProgressModel::Verified).toBool() &&
                                 !completed->data(row, MappingProgressModel::Settling).toBool();
                }
                check(stable,
                      "retained completed row does not animate again during retry or upgrade");
            }
            if (mode == "validation-reject") {
                check(calls.contains("validation rejected automatic binding") && invalidated > 0 &&
                          calls.contains("resolve E started=1 revalidated=99 matched=100") &&
                          !failed,
                      "rejected automatic binding waits and retries without promote/fatal");
            }
            delay(800);
            check(progress.completedCount() == 100 && progress.successful(),
                  "all provisional rows upgrade in place to verified success");
            if (mode == "drift") {
                check(drift == 1 && !failed,
                      "final fingerprint drift starts a generation instead of failure");
                check(calls.contains("resolve E started=1 revalidated=99 matched=100") &&
                          invalidated > 0,
                      "drift retries only affected symbol and retains 99");
            }
            if (mode == "capture-drift")
                check(!failed && calls.contains("detail drift"),
                      "typed capture drift remains recoverable");
        }
        std::printf("Dynamic MappingService: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    return DynamicMappingServiceTests::run(QString::fromLocal8Bit(argv[1]));
}
