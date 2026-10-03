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
    check(opened == 1 && c.steps()[0] == "running", "Attach session requests opening");
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
    std::printf("MappingProgress: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
