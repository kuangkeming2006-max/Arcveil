#include "../../src/MappingService.h"
#include "../../src/MappingProgressController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
struct MappingServiceTests {
    static int run(const QString &fake, const QString &pack, const QString &contracts) {
        int failures = 0, checks = 0;
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok) {
                ++failures;
                std::printf("FAIL %s\n", message);
            }
        };
        QTemporaryDir dir;
        MappingService service;
        // Versioned contract packaging must preserve the v56.8 cache domain,
        // while a changed sub-contract must invalidate the previous proof.
        const auto digest = [](const QString& path) {
            QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QString{};
            return QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex());
        };
        const auto mappingRoot=QFileInfo(contracts).absolutePath();
        service.m_tools=mappingRoot;
        service.m_contracts=mappingRoot+"/contracts-v2.json";
        check(service.activeContractDigest("1.8.9")==digest(contracts),"bundle preserves unchanged legacy cache contract digest");
        check(service.activeContractDigest("1.20.1")==digest(mappingRoot+"/contracts-1.20.1.json"),"modern cache uses its own selected contract digest");
        QFile bundleFile(service.m_contracts); bundleFile.open(QIODevice::ReadOnly);
        auto bundle=QJsonDocument::fromJson(bundleFile.readAll()).object(); bundleFile.close();
        auto versions=bundle.value("versions").toObject();
        auto legacy=versions.value("1.8.9").toObject(); legacy.insert("testContractDrift",true);
        versions.insert("1.8.9",legacy);bundle.insert("versions",versions);
        service.m_contracts=dir.path()+"/changed-contract.json";
        QFile changed(service.m_contracts);changed.open(QIODevice::WriteOnly);changed.write(QJsonDocument(bundle).toJson());changed.close();
        check(service.activeContractDigest("1.8.9")!=digest(contracts),"edited legacy sub-contract invalidates cached proof");
        MappingProgressController progress;
        QObject::connect(&service,&MappingService::eventReceived,&progress,&MappingProgressController::consume);
        bool prematureMatch=false;
        int windows=0, automatic=0, structural=0;
        QStringList captureTiers;
        bool validationScoped=true;
        QObject::connect(&progress,&MappingProgressController::openRequested,[&]{++windows;});
        QObject::connect(&service,&MappingService::eventReceived,[&](const QJsonObject &e){
            if(e.value("event")=="symbol-matched" && service.busy()) prematureMatch=true;
            automatic += e.value("event")=="AUTO_RESOLVE";
            if(e.value("event")=="CAPTURE_REQUEST") {
                const auto tier=e.value("captureTier").toString(); captureTiers.append(tier);
                structural += tier=="structural";
                if(tier=="vanilla-validation" || tier=="modded-validation")
                    validationScoped &= e.value("scopedDictionaryCount").toInt()==1;
            }
        });
        service.m_root = dir.path() + "/cache";
        service.m_analyzer = fake;
        service.m_contracts = contracts;
        service.m_defaultPack = service.m_legacyPack = pack;
        const auto log = dir.path() + "/calls.log";
        qputenv("ARCVEIL_MAPPING_TEST_LOG", log.toUtf8());
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "success");
        int ready = 0, failed = 0, heartbeats = 0;
        QObject::connect(&service, &MappingService::ready,
                         [&](const auto &, const auto &) { ++ready; });
        QObject::connect(&service, &MappingService::failed, [&](const auto &) { ++failed; });
        QTimer heartbeat;
        heartbeat.setInterval(1);
        QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
        heartbeat.start();
        auto wait = [&] {
            QElapsedTimer time;
            time.start();
            while (service.busy() && time.elapsed() < 60000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            return !service.busy();
        };
        auto start = [&] {
            progress.begin(quint32(QCoreApplication::applicationPid()));
            service.prepare(quint32(QCoreApplication::applicationPid()), "unused", "unused", true,
                            {});
        };
        start();
        check(wait() && ready == 1 && !failed, "Mock known Forge authored mapping validates before Ready");
        check(progress.successful() && !prematureMatch,"real service emits verified symbols only after final recheck");
        check(!progress.reference().value("referenceAvailable").toBool(),"first run has no reference");
        check(heartbeats > 0, "event loop responsive during subprocess work");
        QFile before(log);
        before.open(QIODevice::ReadOnly);
        auto first = before.readAll();
        before.close();
        check(first == "inspect\nidentify\ninspect\nvalidate-authored\ninspect\nvalidate-authored\n", "Mock modded preflight detects then independently validates scoped authored mapping twice");
        check(captureTiers==QStringList{"detection","modded-validation","modded-validation"} &&
              validationScoped && !windows && !automatic && !structural && service.m_metadataFastPath &&
              service.m_transaction->detailCaptureCalls==0,
              "Mock authored success uses one dictionary and no Structural Snapshot/resolver/window");
        check(!mapping_cache::Cache(service.m_root).preferred(service.m_contractDigest).valid(),
              "Mock authored metadata receipt is not promoted as a structural Cache proof");
        start();
        progress.stopMatching();
        check(service.busy(),"stop matching leaves preflight running");
        check(wait() && ready == 2, "Mock repeated authored attach independently revalidates without a Cache proof");
        check(progress.successful() && !progress.reference().value("referenceAvailable").toBool(),
              "stopped observer still consumes authored results and injection ready");
        QFile after(log);
        after.open(QIODevice::ReadOnly);
        check(after.readAll() == first + first,
              "Mock repeated attach repeats Detection and two authored validations without structural work");
        after.close();
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "changed");
        start();
        check(wait() && ready == 3 && failed == 0, "Mock unrelated snapshot drift preserves unchanged independently validated mapping identity");
        service.m_root = dir.path() + "/unresolved";
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "unresolved");
        start();
        QElapsedTimer pending;pending.start();
        while(service.m_unresolved.isEmpty() && pending.elapsed()<10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        check(service.busy() && !service.m_unresolved.isEmpty() && ready==3 && failed==0,
              "incomplete resolution waits without authorizing injection or fatal failure");
        check(structural>0 && automatic>0 && windows==1,
              "Mock real authored failure reaches Structural Snapshot/resolver and opens one fallback window");
        service.cancel();
        service.m_root = dir.path() + "/cancelled";
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "slow");
        start();
        QTimer::singleShot(30, &service, &MappingService::cancel);
        check(wait() && ready == 3 && failed == 0, "cancel has no stale ready/failure callback");
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "success");
        start();
        check(wait() && ready == 4, "new generation succeeds after cancellation");
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "flood");
        const auto priorBeats = heartbeats;
        start();
        check(wait() && ready == 5 && heartbeats > priorBeats + 2,
              "JSONL flood remains responsive");
        service.clearEvents();
        check(service.events()->rowCount() == 0, "clear console view");
        service.event({{"event", "progress"}, {"completed", 2}, {"total", 4}});
        check(service.progress() == 0.5 && service.events()->rowCount() == 1, "structured progress reaches console");
        service.event({{"event","SNAPSHOT_STATS"},{"loadedClassCount",123},{"bytecodeBytes",0}});
        check(service.events()->data(service.events()->index(1,0),MappingEventModel::Detail).toString().contains("loadedClassCount"),"capture stats visible in console");
        for (int i=0;i<2010;++i) service.m_events.append({{"event","symbol"},{"symbol",QString::number(i)}});
        check(service.events()->rowCount() == 2000 && service.events()->data(service.events()->index(0,0), MappingEventModel::Symbol).toString() == "10", "console bounds retained events");
        // New recovery events can synchronously cancel the owning transaction.
        // The old choosePack frame must not overwrite Cancelled or start validation.
        service.m_busy = true;
        service.m_skipCacheCandidates = true;
        service.m_cachePath = false;
        const auto recoveryCancel = QObject::connect(&service, &MappingService::eventReceived,
            [&](const QJsonObject &e) { if (e.value("event") == "AUTHORED_REVALIDATE") service.cancel(); });
        service.choosePack();
        QObject::disconnect(recoveryCancel);
        check(!service.busy() && service.status() == "Cancelled" && !service.m_process && ready == 5,
              "authored recovery event cancellation stops old generation before validation");
        check(validationScoped,"Mock every authored validation captures exactly one dictionary");
        const auto priorWindows=windows, priorAutomatic=automatic, priorStructural=structural;
        service.m_root = dir.path() + "/vanilla";
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "vanilla");
        start();
        check(wait() && ready == 6 && failed == 0 && windows == priorWindows && automatic == priorAutomatic && structural == priorStructural &&
              service.m_transaction->detailCaptureCalls == 0 &&
              service.m_transaction->verifiedRuntimeContext.value("validationMode") == "authored-metadata" &&
              !mapping_cache::Cache(service.m_root).preferred(service.m_contractDigest).valid(),
              "Mock Vanilla fast path publishes verified context with no detail/resolver/UI or Cache proof");
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "vanilla-mismatch");
        start();
        check(wait() && ready == 6 && failed == 1 && windows == priorWindows+1 && automatic == priorAutomatic &&
              !service.m_watchTimer.isActive(), "Mock confirmed Vanilla mismatch opens diagnosis and refuses automatic guessing");
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "modern-forge");
        start();
        check(wait() && ready == 6 && failed == 2 && service.m_minecraftVersion == "1.20.1" &&
              service.m_family == "Forge" && service.status().contains("Unsupported") &&
              automatic == priorAutomatic && !service.m_watchTimer.isActive(),
              "Mock 1.20.1 Forge is detected accurately and safely rejected without 1.8.9 fallback/watch");
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "unknown-family");
        start();
        check(wait() && ready == 6 && failed == 3 && automatic == priorAutomatic && !service.m_watchTimer.isActive(),
              "Mock Unknown family cannot select a high-priority provider or infer Vanilla");
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "unverified-version");
        start();
        check(wait() && ready == 6 && failed == 4 && automatic == priorAutomatic,
              "Mock pack-declared version without verified runtime evidence never authorizes Attach");
        QFile oversizedLog(log); oversizedLog.open(QIODevice::WriteOnly | QIODevice::Truncate); oversizedLog.close();
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "oversized-request");
        start();
        check(wait() && ready == 6 && failed == 5 && !service.m_watchTimer.isActive() &&
              service.status().contains("45913") && automatic == priorAutomatic,
              "Mock oversized capture request is terminal with observed/configured diagnostic and no retry/resolver");
        oversizedLog.open(QIODevice::ReadOnly);
        check(oversizedLog.readAll() == "inspect\n", "Mock deterministic request overflow never repeats unchanged requests");
        const auto finalMismatchWindows=windows;
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "vanilla-final-mismatch");
        start();
        check(wait() && ready == 6 && failed == 6 && windows == finalMismatchWindows+1 &&
              automatic == priorAutomatic && !service.m_watchTimer.isActive(),
              "Mock Vanilla final independent descriptor mismatch diagnoses once and never authorizes Ready/resolver");
        std::printf("MappingService: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 4)
        return 2;
    return MappingServiceTests::run(QString::fromLocal8Bit(argv[1]),
                                    QString::fromLocal8Bit(argv[2]),
                                    QString::fromLocal8Bit(argv[3]));
}
