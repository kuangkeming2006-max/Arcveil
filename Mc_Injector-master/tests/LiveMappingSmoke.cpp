#include "../src/OverlayManager.h"
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonDocument>
#include <cstdio>
struct LiveMappingSmoke {
    static void rejectLoader(MappingService *service) {
        const auto instance = quint32(service->m_runtimeBinding.value("instance").toDouble());
        service->m_runtimeBinding["instance"] = double(instance ^ 1U);
    }
    static void cache(MappingService *service, const QString &root) {
        service->m_root = root;
        if (QCoreApplication::arguments().size() == 3)
            service->m_probe = QCoreApplication::arguments().at(2);
    }
    static QJsonObject receipt(MappingService *service) {
        const auto &t = service->m_transaction;
        return {{"cacheHit", t->cacheHit}, {"autoResolveCalls", t->autoResolveCalls},
                {"detailCaptureCalls", t->detailCaptureCalls}, {"totalAttachMs", double(t->elapsed.elapsed())},
                {"stageMs", t->stageMs}, {"analyzerProcesses", t->analyzerProcesses},
                {"helperProcesses", t->helperProcesses}, {"jvmtiCalls", double(t->jvmtiCalls)},
                {"jniCalls", double(t->jniCalls)},
                {"capturedBytes", double(t->capturedBytes)}, {"bytecodeBytes", double(t->bytecodeBytes)},
                {"constantPoolBytes", double(t->constantPoolBytes)}};
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2 || argc > 3)
        return 2;
    bool ok = false;
    auto pid = QString::fromLocal8Bit(argv[1]).toUInt(&ok);
    if (!ok || !pid)
        return 2;
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir prefs;
    if (!prefs.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, prefs.path());
    OverlayManager manager;
    QTemporaryDir cache(QCoreApplication::applicationDirPath() + "/live-mapping-XXXXXX");
    if (!cache.isValid())
        return 2;
    cache.setAutoRemove(false);
    const auto cachePath = qEnvironmentVariable("ARCVEIL_LIVE_CACHE", cache.path());
    const auto scenario = qEnvironmentVariable("ARCVEIL_LIVE_SCENARIO", "cold-and-same-pid");
    const bool rejectLoader = qEnvironmentVariableIsSet("ARCVEIL_LIVE_REJECT_LOADER");
    const auto cycles = qMax(1, qEnvironmentVariableIntValue("ARCVEIL_LIVE_CYCLES") == 0 ? 2 : qEnvironmentVariableIntValue("ARCVEIL_LIVE_CYCLES"));
    std::printf("LIVE_CACHE %s\n", cachePath.toUtf8().constData());
    LiveMappingSmoke::cache(manager.mappingService(), cachePath);
    bool finished = false, passed = false;
    int cycle = 1, automaticCalls = 0, cacheHits = 0;
    bool awaitingDetach = false;
    QObject::connect(manager.mappingService(), &MappingService::eventReceived,
                     [&](const QJsonObject &event) {
                         if (event.value("event") == "AUTO_RESOLVE") ++automaticCalls;
                         if (event.value("event") == "CACHE_LOOKUP" && event.value("hit").toBool()) ++cacheHits;
                         if (rejectLoader && event.value("event") == "validation" && event.value("stage") == "verified")
                             LiveMappingSmoke::rejectLoader(manager.mappingService());
                         auto text = QJsonDocument(event).toJson(QJsonDocument::Compact);
                         std::printf("%s\n", text.constData());
                         std::fflush(stdout);
                     });
    auto stop = [&](bool success) {
        if (finished)
            return;
        finished = true;
        passed = success;
        std::printf(
            "LIVE_RESULT success=%d profile=%s mappingState=%s renderer=%d error=%s detail=%s\n",
            success, manager.mappingProfile().toUtf8().constData(),
            manager.mappingState().toUtf8().constData(), manager.rendererActive(),
            manager.errorCode().toUtf8().constData(), manager.errorDetail().toUtf8().constData());
        std::fflush(stdout);
        manager.detach();
        QTimer::singleShot(4000, &app, [&] { app.exit(passed ? 0 : 1); });
    };
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (awaitingDetach) {
            if (manager.state() == OverlayManager::State::Detached) {
                awaitingDetach = false;
                ++cycle; automaticCalls = 0; cacheHits = 0;
                if (!manager.attachToProcess(pid)) stop(false);
            }
            return;
        }
        if (manager.state() == OverlayManager::State::Error) {
            const bool rejected = rejectLoader && manager.errorCode() == "RUNTIME_BINDING_FAILED" &&
                manager.transactionState() != "Active" && !manager.rendererActive();
            if (rejected) std::printf("NEGATIVE_BINDING_ACCEPTANCE wrongLoaderRejected=true active=false\n");
            stop(rejected);
        }
        else if (manager.rendererActive() && manager.transactionState() == "Active" &&
                 !manager.mappingService()->busy() && !manager.mappingProfile().isEmpty() &&
                 (manager.mappingState() == "ready" || manager.mappingState() == "no_player" ||
                  manager.mappingState() == "no_world"))
        {
            if (rejectLoader) { stop(false); return; }
            auto receipt = LiveMappingSmoke::receipt(manager.mappingService());
            receipt["event"] = "LUNAR_ACCEPTANCE";
            receipt["scenario"] = scenario;
            receipt["cycle"] = cycle;
            receipt["pid"] = double(pid);
            receipt["processStart"] = MappingService::processStartFor(pid);
            receipt["profile"] = manager.mappingProfile();
            receipt["mappingState"] = manager.mappingState();
            receipt["rendererActive"] = manager.rendererActive();
            receipt["transactionId"] = manager.transactionId();
            std::printf("%s\n", QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData());
            std::printf("ATTACH_ACCEPTANCE cycle=%d autoResolveCalls=%d cacheHits=%d transaction=%s\n",
                cycle, automaticCalls, cacheHits, manager.transactionId().toUtf8().constData());
            std::printf("AGENT_MAPPING profile=%s state=%s renderer=%d\n", manager.mappingProfile().toUtf8().constData(), manager.mappingState().toUtf8().constData(), manager.rendererActive());
            std::fflush(stdout);
            const bool expectHit = cycle > 1 || scenario.startsWith("restart") ||
                scenario.startsWith("same-pid") || scenario.startsWith("legacy-proof");
            if (expectHit && (automaticCalls != 0 || cacheHits == 0 ||
                !receipt.value("cacheHit").toBool() || receipt.value("detailCaptureCalls").toInt() != 0)) stop(false);
            else if (cycle < cycles) { awaitingDetach = true; manager.detach(); }
            else stop(true);
        }
    });
    poll.start();
    QObject::connect(&manager, &OverlayManager::statusMessageChanged, [&] {
        std::printf("STATUS %s\n", manager.statusMessage().toUtf8().constData());
        std::fflush(stdout);
    });
    QTimer::singleShot(600000, &app, [&] { stop(false); });
    QTimer::singleShot(0, &app, [&] {
        if (!manager.attachToProcess(pid))
            stop(false);
    });
    return app.exec();
}
