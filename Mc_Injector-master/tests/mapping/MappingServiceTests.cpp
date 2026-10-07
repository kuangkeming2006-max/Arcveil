#include "../../src/MappingService.h"
#include "../../src/MappingProgressController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
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
        MappingProgressController progress;
        QObject::connect(&service,&MappingService::eventReceived,&progress,&MappingProgressController::consume);
        bool prematureMatch=false;
        QObject::connect(&service,&MappingService::eventReceived,[&](const QJsonObject &e){
            if(e.value("event")=="symbol-matched" && service.busy()) prematureMatch=true;
        });
        service.m_root = dir.path() + "/cache";
        service.m_analyzer = fake;
        service.m_contracts = contracts;
        service.m_defaultPack = pack;
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
        check(wait() && ready == 1 && !failed, "known pack validated before ready");
        check(progress.successful() && !prematureMatch,"real service emits verified symbols only after final recheck");
        check(!progress.reference().value("referenceAvailable").toBool(),"first run has no reference");
        check(heartbeats > 0, "event loop responsive during subprocess work");
        QFile before(log);
        before.open(QIODevice::ReadOnly);
        auto first = before.readAll();
        before.close();
        check(first == "inspect\nidentify\nselect\ninspect-detail\nvalidate\ninspect\nselect\ninspect-detail\n", "preflight ordering");
        start();
        progress.stopMatching();
        check(service.busy(),"stop matching leaves preflight running");
        check(wait() && ready == 2, "verified cache hit");
        check(progress.successful() && progress.reference().value("referenceAvailable").toBool(),
              "stopped observer still consumes cached results and injection ready");
        QFile after(log);
        after.open(QIODevice::ReadOnly);
        check(after.readAll() == first + "inspect\nidentify\nselect\ninspect-detail\nvalidate\ninspect\nselect\ninspect-detail\n",
              "cache hit still performs final runtime recheck");
        after.close();
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "changed");
        start();
        check(wait() && ready == 3 && failed == 0, "final fingerprint drift revalidates and recovers without failure");
        service.m_root = dir.path() + "/unresolved";
        qputenv("ARCVEIL_MAPPING_TEST_MODE", "unresolved");
        start();
        QElapsedTimer pending;pending.start();
        while(service.m_unresolved.isEmpty() && pending.elapsed()<10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        check(service.busy() && !service.m_unresolved.isEmpty() && ready==3 && failed==0,
              "incomplete resolution waits without authorizing injection or fatal failure");
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
