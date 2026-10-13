#include "../../src/MappingProgressController.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *s) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", s);
        }
    };
    auto wait = [] {
        QEventLoop loop;
        QTimer::singleShot(800, &loop, &QEventLoop::quit);
        loop.exec();
    };
    MappingProgressController c;
    int opened = 0, stopped = 0;
    QObject::connect(&c, &MappingProgressController::openRequested, [&] { ++opened; });
    QObject::connect(&c, &MappingProgressController::matchingStopped, [&] { ++stopped; });
    c.begin(123);
    check(opened == 0 && c.steps()[0] == "running", "normal Attach initializes presentation without opening");
    check(!c.reference().value("referenceAvailable").toBool(), "no reference explicit");
    auto queue = [&](QString s, bool required) {
        c.consume({{"event", "symbol-queued"}, {"symbol", s}, {"required", required}});
    };
    auto start = [&](QString s) { c.consume({{"event", "symbol-started"}, {"symbol", s}}); };
    auto match = [&](QString s, QString runtime) {
        c.consume({{"event", "symbol-matched"},
                   {"symbol", s},
                   {"runtimeName", runtime},
                   {"verified", true},
                   {"confidence", 0.99}});
    };
    queue("Minecraft.thePlayer", true);
    queue("World.players", true);
    queue("optional", false);
    check(c.pendingCount() == 3 && c.pending()->rowCount() == 3, "schema populates pending proxy");
    start("Minecraft.thePlayer");
    check(c.activeCount() == 1 && c.pendingCount() == 2, "pending to active");
    c.consume(
        {{"event", "symbol-matched"}, {"symbol", "Minecraft.thePlayer"}, {"runtimeName", "wrong"}});
    check(c.completedCount() == 0, "unverified provisional result cannot complete");
    match("Minecraft.thePlayer", "ave.f");
    check(c.activeCount() == 1 && c.completedCount() == 0, "matched result dwells in active");
    wait();
    check(c.completedCount() == 1 && c.activeCount() == 0 && c.completed()->rowCount() == 1,
          "timer migrates result even while window hidden");
    c.consume({{"event", "snapshot-update"}, {"fingerprint", "first"}});
    start("World.players");
    c.consume({{"event", "snapshot-update"}, {"fingerprint", "changed"}});
    check(c.activeCount() == 1 && c.completedCount() == 1,
          "snapshot observation alone does not invent retries in UI");
    c.consume({{"event", "symbol-retry"}, {"symbol", "World.players"}});
    check(c.pendingCount() == 2 && c.completedCount() == 1, "service retries unfinished only");
    auto completed = c.completed();
    check(completed->data(completed->index(0, 0), MappingProgressModel::RuntimeName) == "ave.f",
          "completed value stable");
    start("World.players");
    c.consume({{"event", "snapshot-update"}, {"fingerprint", "changed"}});
    check(c.activeCount() == 1, "identical snapshot does not retry");
    c.stopMatching();
    int prior = stopped;
    c.consume({{"event", "snapshot-update"}, {"fingerprint", "third"}});
    check(!c.matchingEnabled() && c.activeCount() == 1 && c.completedCount() == 1,
          "stop disables retries without clearing in-flight or completed results");
    match("World.players", "bdb.j");
    check(c.successful() && !c.matchingEnabled() && c.steps()[3] == "success" &&
              stopped == prior + 1,
          "all required verified stops matching and succeeds");
    wait();
    check(c.completedCount() == 2 && c.pendingCount() == 1,
          "optional unresolved does not prevent success");
    match("Minecraft.thePlayer", "bad");
    check(completed->data(completed->index(0, 0), MappingProgressModel::RuntimeName) == "ave.f",
          "duplicate accepted result never replaces completed");
    c.begin(456);
    queue("old", true);
    match("old", "a");
    c.begin(789);
    queue("new", true);
    wait();
    check(c.completedCount() == 0 && c.pendingCount() == 1,
          "old migration timer cannot corrupt new Attach");
    c.consume({{"event", "complete"}, {"command", "inspect"}, {"success", true}});
    check(!c.successful(), "subcommand completion is not mapping completion");
    c.consume({{"event", "failure"}, {"reason", "fixture failure"}});
    check(c.steps()[3] == "failed" && c.status() == "fixture failure", "failure visible");
    c.begin(900);
    c.consume({{"event", "cancelled"}}); // Previous preflight teardown following a new Attach.
    const int windowsBefore = opened;
    c.consume({{"event", "session-start"}, {"pid", 900}});
    check(c.matchingEnabled() && opened == windowsBefore,
          "service start resets stale cancellation without reopening a hidden window");
    c.stopMatching();
    c.consume({{"event", "session-start"}, {"pid", 900}});
    check(!c.matchingEnabled(), "manual stop survives delayed session start");
    c.begin(1000);
    queue("provisional", true);
    c.consume({{"event", "symbol-matched"},
               {"symbol", "provisional"},
               {"runtimeName", "runtime.foo"},
               {"provisional", true},
               {"verified", false}});
    wait();
    check(c.completedCount() == 1 && !c.successful(),
          "provisional row completes animation without injection success");
    c.consume({{"event", "symbol-revalidated"}, {"symbol", "provisional"}, {"provisional", true}});
    check(c.completedCount() == 1 &&
              !c.completed()
                   ->data(c.completed()->index(0, 0), MappingProgressModel::Settling)
                   .toBool(),
          "revalidation preserves completed row without animation");
    c.consume({{"event", "symbol-matched"},
               {"symbol", "provisional"},
               {"runtimeName", "runtime.foo"},
               {"verified", true}});
    check(c.successful() && c.completedCount() == 1 &&
              c.completed()
                  ->data(c.completed()->index(0, 0), MappingProgressModel::Verified)
                  .toBool(),
          "final receipt upgrades provisional in place");
    c.begin(1001);
    queue("affected", true);
    c.consume({{"event", "symbol-matched"},
               {"symbol", "affected"},
               {"runtimeName", "runtime.old"},
               {"provisional", true}});
    c.consume(
        {{"event", "symbol-invalidated"}, {"symbol", "affected"}, {"reason", "evidence changed"}});
    wait();
    check(c.completedCount() == 0 && c.pendingCount() == 1,
          "invalidated migration timer cannot resurrect a binding");
    c.consume({{"event", "session-start"}, {"transactionId", "A"}});
    c.begin(1002);
    const auto startingStatus = c.status();
    c.consume({{"event", "cancelled"}, {"transactionId", "A"}});
    check(c.status() == startingStatus, "new Attach presentation rejects prior cancellation before session-start");
    c.consume({{"event", "session-start"}, {"transactionId", "B"}});
    c.consume({{"event", "symbol-queued"}, {"transactionId", "A"}, {"symbol", "old"}, {"required", true}});
    check(c.pendingCount() == 0, "old transaction cannot change new session symbol UI");
    const int beforeFastPath = opened;
    c.begin(1003, "fast-path");
    const auto fastPathStatus = c.status();
    c.consume({{"event", "session-start"}, {"transactionId", "B"}, {"pid", 1002}});
    c.consume({{"event", "mapping-fallback"}, {"transactionId", "B"},
               {"mode", "automatic"}, {"reason", "stale fallback"}});
    check(opened == beforeFastPath && c.status() == fastPathStatus,
          "new owner UUID rejects delayed prior session-start and fallback events");
    c.consume({{"event", "session-start"}, {"transactionId", "fast-path"}, {"pid", 1003}});
    c.consume({{"event", "CACHE_LOOKUP"}, {"transactionId", "fast-path"}, {"hit", true}});
    c.consume({{"event", "symbol-queued"}, {"transactionId", "fast-path"}, {"symbol", "required"}, {"required", true}});
    c.consume({{"event", "symbol-matched"}, {"transactionId", "fast-path"},
               {"symbol", "required"}, {"runtimeName", "ave.f"}, {"verified", true}});
    c.consume({{"event", "complete"}, {"transactionId", "fast-path"},
               {"scope", "mapping"}, {"source", "verified-cache"}});
    check(c.successful() && opened == beforeFastPath, "verified Cache fast path succeeds without automatic Mapping window");
    c.begin(1004, "authored");
    c.consume({{"event", "session-start"}, {"transactionId", "authored"}, {"pid", 1004}});
    c.consume({{"event", "CACHE_LOOKUP"}, {"transactionId", "authored"}, {"hit", false}});
    c.consume({{"event", "symbol-queued"}, {"transactionId", "authored"}, {"symbol", "required"}, {"required", true}});
    c.consume({{"event", "symbol-matched"}, {"transactionId", "authored"},
               {"symbol", "required"}, {"runtimeName", "ave.f"}, {"verified", true}});
    c.consume({{"event", "complete"}, {"transactionId", "authored"},
               {"scope", "mapping"}, {"source", "authored"}});
    check(c.successful() && opened == beforeFastPath && c.status().startsWith("Mapping"),
          "authored revalidation succeeds silently with validation status");
    c.begin(1005, "fallback");
    c.consume({{"event", "session-start"}, {"transactionId", "fallback"}, {"pid", 1005}});
    const QJsonObject fallback{{"event", "mapping-fallback"}, {"transactionId", "fallback"},
                               {"mode", "automatic"}, {"reason", "authored dictionaries did not match"}};
    c.consume(fallback);
    check(opened == beforeFastPath + 1 && c.status() == "authored dictionaries did not match",
          "real automatic fallback requests the existing window");
    c.stopMatching();
    c.consume(fallback);
    c.consume({{"event", "snapshot-update"}, {"transactionId", "fallback"}, {"fingerprint", "retry"}});
    check(opened == beforeFastPath + 1 && !c.matchingEnabled(),
          "fallback retry never reopens a user-hidden window or resumes paused matching");
    c.open();
    check(opened == beforeFastPath + 2, "manual presentation opt-in remains available");
    c.begin(1006, "vanilla-diagnostic");
    c.consume({{"event", "session-start"}, {"transactionId", "vanilla-diagnostic"}, {"pid", 1006}});
    c.consume({{"event", "mapping-fallback"}, {"transactionId", "vanilla-diagnostic"},
               {"mode", "diagnostic"}, {"reason", "Vanilla 1.8.9 required Entity descriptor differs"}});
    check(opened == beforeFastPath + 3 && !c.matchingEnabled() && c.status().contains("descriptor"),
          "Vanilla mapping mismatch shows diagnosis without enabling automatic matching");
    std::printf("MappingProgress: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
