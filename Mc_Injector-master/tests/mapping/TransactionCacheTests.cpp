#include "../../src/MappingService.h"
#include "../../src/MappingProgressController.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
using namespace mapping_cache;
struct TransactionCacheTests {
    static int run(const QString &worker, const QString &fixture, const QString &contracts) {
        int failures = 0, checks = 0;
        auto check = [&](bool ok, const char *message) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", message); } };
        auto spin = [](auto done, int ms = 60000) { QElapsedTimer t; t.start(); while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10); return done(); };
        auto delay = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
        QTemporaryDir temp;
        MappingService service;
        service.m_root = temp.path() + "/cache"; service.m_analyzer = worker;
        service.m_defaultPack = fixture + "/lunar-pack.json"; service.m_contracts = contracts;
        service.m_watchInterval = 20; service.m_maxWatchInterval = 60; service.m_debounceMs = 5;
        MappingProgressController progress;
        QObject::connect(&service, &MappingService::eventReceived, &progress, &MappingProgressController::consume);
        int ready = 0, failed = 0, autoCalls = 0, hits = 0, promoted = 0;
        QObject::connect(&service, &MappingService::ready, [&](const auto &, const auto &) { ++ready; });
        QObject::connect(&service, &MappingService::failed, [&](const auto &s) { ++failed; std::printf("FAILURE %s\n", s.toUtf8().constData()); });
        QObject::connect(&service, &MappingService::eventReceived, [&](const QJsonObject &e) {
            if (e.value("event") == "CACHE_LOOKUP" && !e.value("hit").toBool())
                std::printf("CACHE_MISS %s\n", e.value("reason").toString().toUtf8().constData());
            autoCalls += e.value("event") == "AUTO_RESOLVE";
            hits += e.value("event") == "CACHE_LOOKUP" && e.value("hit").toBool();
            promoted += e.value("event") == "CACHE_PROMOTE";
        });
        qputenv("ARCVEIL_TRANSACTION_LOG", (temp.path() + "/calls.log").toUtf8());
        qputenv("ARCVEIL_TRANSACTION_FAMILY", "Lunar"); qputenv("ARCVEIL_TRANSACTION_LOADER", "123");
        qputenv("ARCVEIL_TRANSACTION_RAW", (fixture + "/renamed-raw.json").toUtf8());
        auto start = [&] { progress.begin(QCoreApplication::applicationPid()); service.prepare(QCoreApplication::applicationPid(), "fixture", "fixture", true, {}); };
        // Verified same-family original-build reference; target renamed build has no cache entry.
        Cache(service.m_root).promote(service.m_defaultPack, fixture + "/lunar-reference.json", "source", fileDigest(contracts),
            {{"valid", true}, {"injectionReady", true}, {"fingerprint", "source"}}, "source-id", "source-meta", "Lunar", "1.8.9");
        start();
        check(spin([&] { return ready || failed; }) && ready == 1 && failed == 0 && autoCalls == 1 && promoted == 1,
              "A: cache miss -> real automatic resolver -> real validate -> atomic promote -> ready");
        const auto firstIdentity = service.m_mappingIdentity;
        autoCalls = 0; hits = 0; start();
        check(spin([&] { return ready == 2 || failed; }) && ready == 2 && autoCalls == 0 && hits == 1,
              "B: second same-build attach uses scoped real validation with zero automatic resolve calls");
        check(progress.successful() && progress.status().contains("Cache HIT"),
              "B: fresh live validation retains Cache HIT -> Live validation -> Ready presentation");
        check(service.m_transaction->detailCaptureCalls == 0 && !service.m_hit.bindingIdentity.isEmpty(),
              "B: verified installed proof reuses mapping with zero detail captures");
        // Fresh loader instance + extra class + enumeration order differ, identity is unchanged.
        qputenv("ARCVEIL_TRANSACTION_EXTRA", "1"); qputenv("ARCVEIL_TRANSACTION_LOADER", "98765");
        autoCalls = 0; hits = 0; start();
        check(spin([&] { return ready == 3 || failed; }) && ready == 3 && autoCalls == 0 && hits == 1 && service.m_mappingIdentity == firstIdentity,
              "C: class count/order/loader instance do not affect reusable mapping identity");
        check(service.m_transaction->detailCaptureCalls == 0 && service.m_runtimeBinding.value("instance").toDouble() == 98765,
              "C: restart fast check pins current loader and performs zero detail captures");
        check(!Cache(service.m_root).reference(fileDigest(contracts), "Badlion", "1.8.9").valid(),
              "D: Badlion never takes Lunar lastVerified as a family reference");
        // Cancel while real Analyzer capture is in flight, then check no cache publication/ready.
        qputenv("ARCVEIL_TRANSACTION_SLOW", "inspect");
        const int beforeReady = ready, beforePromote = promoted;
        start(); const auto oldTransaction = service.m_transaction;
        QTimer::singleShot(40, &service, &MappingService::cancel);
        check(spin([&] { return !service.busy(); }) && !oldTransaction->valid && !service.m_watchTimer.isActive(), "E: cancellation invalidates ownership and stops capture/watch");
        delay(150);
        check(ready == beforeReady && promoted == beforePromote, "E: cancellation has no ready or promotion");
        // Superseding transaction starts immediately; old subprocess/queued output cannot change it.
        start(); const auto a = service.transactionId(); service.cancel();
        qunsetenv("ARCVEIL_TRANSACTION_SLOW"); start(); const auto b = service.transactionId();
        check(spin([&] { return ready > beforeReady || failed; }) && a != b && ready == beforeReady + 1, "F: A cancelled, B succeeds with unique ownership");
        // G: close the real MappingProgressWindow while capture is pending.
        qmlRegisterSingletonInstance("McOverlay", 1, 0, "MappingProgress", &progress);
        qmlRegisterSingletonInstance("McOverlay", 1, 0, "MappingService", &service);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(contracts).absolutePath() + "/../qml/MappingProgressWindow.qml"));
        QVariantMap colors{{"backgroundColor","#181c20"},{"surfaceColor","#252a30"},{"textColor","#ffffff"},
            {"secondaryTextColor","#aaaaaa"},{"outlineColor","#404650"},{"outlineVariantColor","#404650"}};
        std::unique_ptr<QObject> window(component.createWithInitialProperties({{"host",colors}}));
        if (!window) std::printf("QML %s\n", component.errorString().toUtf8().constData());
        qputenv("ARCVEIL_TRANSACTION_SLOW", "inspect");
        autoCalls = 0; start();
        const auto gTransaction = service.transactionId();
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
        if (quickWindow) { quickWindow->show(); delay(20); quickWindow->close(); }
        check(quickWindow && service.busy() && service.transactionId() == gTransaction && service.matchingEnabled(),
              "G: closing real MappingProgressWindow preserves transaction and automatic matching");
        qunsetenv("ARCVEIL_TRANSACTION_SLOW");
        check(spin([&] { return ready == beforeReady + 2 || failed; }) && autoCalls == 0, "G: progress presentation lifecycle does not control transaction");
        // Missing class scenario: remove reference so required mappings remain incomplete.
        service.m_root = temp.path() + "/waiting"; start();
        check(spin([&] { return service.m_watchTimer.isActive() || failed; }), "H: reaches incomplete real selection/resolution");
        service.stopMatching(); const auto transaction = service.transactionId();
        check(service.busy() && service.m_transaction->state == AttachTransaction::State::MappingPaused, "H: pause retains pending Attach");
        QFile log(temp.path() + "/calls.log"); log.open(QIODevice::ReadOnly); const auto bytes = log.readAll(); log.close(); delay(120);
        log.open(QIODevice::ReadOnly); check(log.readAll() == bytes && service.transactionId() == transaction, "H: paused transaction starts no retry"); log.close();
        service.resumeMatching(); check(service.busy() && service.matchingEnabled(), "H: resume keeps transaction"); service.cancel();
        // Pause before the first retry is armed: resume must install a fresh timer owner.
        service.m_root = temp.path() + "/early-pause"; start(); service.stopMatching();
        check(spin([&] { return !service.m_process && service.status().contains("waiting"); }) && service.busy() && !service.m_watchTimer.isActive(),
              "H: early pause preserves incomplete Attach without an armed watch timer");
        const auto earlySerial = service.m_captureSerial; service.resumeMatching();
        check(spin([&] { return service.m_captureSerial > earlySerial; }), "H: early-pause Resume actually starts the next capture");
        service.cancel();
        // Client update that preserves metadata but changes installed constants: reject cached
        // identity before resolving against a new verified same-family reference.
        service.m_root = temp.path() + "/cache";
        qputenv("ARCVEIL_TRANSACTION_RAW", (fixture + "/updated-raw.json").toUtf8());
        const auto updatedPack = fixture + "/updated-pack.json", updatedReference = fixture + "/updated-reference.json";
        Cache(service.m_root).promote(updatedPack, updatedReference, "updated-source", fileDigest(contracts),
            {{"valid", true}, {"injectionReady", true}, {"fingerprint", "updated-source"}}, "updated-source-id", "different-source-meta", "Lunar", "1.8.9");
        const auto previousReady = ready, previousPromote = promoted; autoCalls = 0; hits = 0; start();
        check(spin([&] { return ready > previousReady || failed; }) && ready == previousReady + 1 && autoCalls == 1 && promoted == previousPromote + 1 && service.m_mappingIdentity != firstIdentity,
              "J: cached live structural identity rejected -> automatic resolve -> new atomic promotion");
        const auto legacySource = service.m_hit;
        service.m_root = temp.path() + "/legacy";
        Cache(service.m_root).promote(legacySource.pack, legacySource.snapshot, legacySource.fingerprint, fileDigest(contracts),
            {{"valid", true}, {"injectionReady", true}, {"fingerprint", legacySource.fingerprint}},
            legacySource.mappingIdentity, legacySource.metadataIdentity, legacySource.family, legacySource.minecraftVersion);
        const auto legacyReady = ready, legacyPromoted = promoted; autoCalls = 0; hits = 0; start();
        check(spin([&] { return ready > legacyReady || failed; }) && ready == legacyReady + 1 && autoCalls == 0 && hits == 1 &&
            service.m_transaction->detailCaptureCalls == 0 && promoted == legacyPromoted + 1 &&
            !Cache(service.m_root).preferred(fileDigest(contracts)).bindingIdentity.isEmpty(),
            "K: PR #6 verified source upgrades offline, compact live validation, atomic proof publication, zero details");
        check(failed == 0, "all real transaction/cache scenarios complete without unexpected failure");
        // Unsupported API is terminal, even when names/descriptors match a fixture.
        auto unsupportedPack=readObject(service.m_defaultPack);
        unsupportedPack["gameVersion"]="1.12.2";
        service.m_defaultPack=temp.path()+"/unsupported-pack.json";
        writeObject(service.m_defaultPack,unsupportedPack);
        service.m_root=temp.path()+"/unsupported-cache";
        const auto unsupportedReady=ready, unsupportedPromoted=promoted;
        start();
        check(spin([&]{return failed>0;}) && failed==1 && ready==unsupportedReady && promoted==unsupportedPromoted &&
              !service.busy() && !service.m_watchTimer.isActive() && service.status().contains("Version Adapter"),
              "L: unsupported API reports terminal failure, never ready/promote/retry");
        std::printf("Transaction/cache: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
};
int main(int argc, char **argv) { QGuiApplication app(argc, argv); if (argc != 4) return 2; return TransactionCacheTests::run(argv[1], argv[2], argv[3]); }
