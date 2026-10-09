"""Summarize real LiveMappingSmoke logs; fixtures are not performance evidence.

Usage: python Verify-LunarAcceptance.py cold-and-same.log restart.log
"""
import hashlib
import json
import sys
from pathlib import Path


def receipts(path):
    data = Path(path).read_bytes()
    events = []
    for line in data.decode("utf-8-sig").splitlines():
        if line.startswith("{"):
            events.append(json.loads(line))
    assert any(e.get("event") == "SNAPSHOT_STATS" and e.get("jvmtiCalls", 0) > 0 for e in events), "No live probe statistics"
    return [e for e in events if e.get("event") == "LUNAR_ACCEPTANCE"], hashlib.sha256(data).hexdigest()


def main():
    first, first_hash = receipts(sys.argv[1])
    restarted, restart_hash = receipts(sys.argv[2])
    assert len(first) == 2 and len(restarted) == 1, "Expected cold/same-PID and restart receipts"
    cold, same = first
    restart = restarted[0]
    assert not cold["cacheHit"], "Cold acceptance reused a cache"
    assert cold["detailCaptureCalls"] > 0, "Cold acceptance bypassed full capture"
    assert (cold["pid"], cold["processStart"]) == (same["pid"], same["processStart"]), "Same-PID identity changed"
    assert (same["pid"], same["processStart"]) != (restart["pid"], restart["processStart"]), "Restart reused the original process"
    for row in (cold, same, restart):
        assert row["rendererActive"] and row["profile"].startswith("Lunar "), "Agent/renderer not usable"
        assert row["mappingState"] in ("ready", "no_player", "no_world"), "Binding state is not usable"
        assert row["stageMs"]["jni binding"] > 0 and row["jniCalls"] > 0, "No final JNI binding receipt"
        assert row["totalAttachMs"] > 0 and row["jvmtiCalls"] > 0
    for row in (same, restart):
        assert row["cacheHit"] and row["autoResolveCalls"] == 0 and row["detailCaptureCalls"] == 0, "Cache fast path fell back"
    for scenario, row in zip(("cold", "same-pid", "restart"), (cold, same, restart)):
        print(json.dumps({**row, "scenario": scenario,
                          "baselineMs": 49194, "reductionPercent": round((1 - row["totalAttachMs"] / 49194) * 100, 2)}, ensure_ascii=False))
    print(json.dumps({"rawLogSha256": {Path(sys.argv[1]).name: first_hash, Path(sys.argv[2]).name: restart_hash}}))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main()
