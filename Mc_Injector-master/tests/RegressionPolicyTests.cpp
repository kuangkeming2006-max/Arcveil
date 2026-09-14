#include "../agent/bindings/SafeWalkPolicy.h"
#include "../agent/UiPreferences.h"
#include "../src/ApiKeyFormat.h"
#include <cmath>
#include <cstdio>

int main()
{
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
    };
    using namespace mcoverlay;
    check(unpackUiPreferences(0x43U).blur == 65, "old config retains default blur");
    for (int blur : {0, 25, 65, 100}) for (int x : {0, 128, 255}) for (int y : {0, 128, 255}) {
        auto bits = packUiPreferences({blur, x, y});
        auto p = unpackUiPreferences(bits | 0xffU);
        check(p.blur == blur && p.imeX == x && p.imeY == y, "preferences exact roundtrip incl high unsigned bit");
        check((bits & 0xffU) == 0, "new preferences never overwrite feature flags");
    }
    check(normalizeHypixelApiKey(QString(1024, 'x')).size() == 1024,
          "personal key opaque >256 bytes accepted");
    check(normalizeHypixelApiKey("  test_token.a-b_c  ") == "test_token.a-b_c", "credential not rewritten");
    check(normalizeHypixelApiKey("a\r\nAPI-Key: bad").isEmpty(), "reject header injection");
    check(normalizeHypixelApiKey(QString(8193, 'x')).isEmpty(), "bounded credential input");
    check(normalizeHypixelApiKey(QString::fromUtf8("错误key")).isEmpty(), "reject accidental Unicode paste");

    // Box collision oracle: one 1x1 cube under a standard 0.6m player body.
    // Sweep all four cardinal directions and both positive/negative corners.
    for (int sx : {-1, 0, 1}) for (int sz : {-1, 0, 1}) {
        if (sx == 0 && sz == 0) continue;
        double previousTrigger = -1;
        for (int sensitivity : {0, 25, 50, 75, 100}) {
            double trigger = 2;
            for (int step = 0; step <= 1400; ++step) {
                const double distance = step * 0.001;
                const double cx = 0.5 + sx * distance, cz = 0.5 + sz * distance;
                const auto supported = [&](double dx, double dz, double inset) {
                    return cx + dx + 0.3 - inset > 0 && cx + dx - 0.3 + inset < 1 &&
                           cz + dz + 0.3 - inset > 0 && cz + dz - 0.3 + inset < 1;
                };
                if (safewalk::needsSneak(sensitivity, sx * 0.09, sz * 0.09,
                                         sx * 0.7, sz * 0.7, supported)) { trigger = distance; break; }
            }
            check(trigger >= previousTrigger - 0.001, "higher sensitivity triggers later, not earlier");
            check(trigger < 0.8, "all sensitivities guard before physical body leaves support");
            previousTrigger = trigger;
        }
    }
    check(safewalk::predictedMotion(0.2, 0) == 0.2, "released-key inertia remains in prediction");
    check(safewalk::predictedMotion(0.16, 1) > 0.25, "next tick input acceleration is included");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
