"""Offline production request-planner tests; never attaches to a JVM.

The 1,200 history packs deliberately have unique symbols/hints and large warmup
sets. A detection request must ignore them, and a validation request must derive
its complete required scope only from the selected authored/cached pack.
"""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import time


build = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(__file__).resolve().parents[2]
analyzer = build / "MappingAnalyzer.exe"
packs = source / "mapping" / "packs"
contracts = source / "mapping" / "contracts-v2.json"
legacy = packs / "default-v2.json"
modern = packs / "vanilla-1.20.1.json"
checks = 0


def check(condition, message):
    global checks
    checks += 1
    assert condition, message


def plan(tier, pack=None, catalog=None, extra=()):
    args = [str(analyzer), "inspect", "--plan-capture", "--capture-tier", tier,
            "--contracts", str(contracts)]
    if pack is not None:
        args += ["--pack", str(pack)]
    if catalog is not None:
        args += ["--identity-packs", str(catalog)]
    args += list(extra)
    started = time.perf_counter()
    completed = subprocess.run(args, text=True, encoding="utf-8", capture_output=True)
    check(completed.returncode == 0, completed.stdout + completed.stderr)
    records = [json.loads(line) for line in completed.stdout.splitlines() if line.strip()]
    payload = next((row for row in reversed(records) if "request" in row), None)
    if payload is None:
        raise AssertionError("--plan-capture must emit request + stats: " + completed.stdout)
    request = payload["request"]
    stats = payload.get("stats", payload.get("requestStats", {}))
    encoded = json.dumps(request, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    check(stats["requestBytes"] == len(encoded), "request byte diagnostics do not describe the emitted request")
    check(stats["requestedClassCount"] == len(request.get("liteClasses", [])), "class count diagnostics drift")
    check(stats["runtimeAnchorCount"] == len(request.get("runtimeAnchors", [])), "anchor count diagnostics drift")
    check(stats["warmupClassCount"] == sum(len(row["classes"]) for row in request.get("warmup", [])),
          "warmup count diagnostics drift")
    # A planning receipt has a fixed ID, independent of a live transaction UUID.
    return request, stats, round((time.perf_counter() - started) * 1000, 3)


with tempfile.TemporaryDirectory(prefix="arcveil-request-stress-", dir=build) as directory:
    directory = pathlib.Path(directory)
    small = directory / "one-pack.json"
    history = directory / "1200-history-packs.json"
    selected = directory / "selected-cache-pack.json"
    selected_object = json.loads(legacy.read_text(encoding="utf-8"))
    selected_object["providers"] = [copy.deepcopy(selected_object["providers"][-1])]
    selected_object["providers"][0]["dictionaries"] = [selected_object["providers"][0]["dictionaries"][0]]
    selected.write_text(json.dumps(selected_object), encoding="utf-8")
    legacy_contract = json.loads((source / "mapping" / "contracts-v1.json").read_text(encoding="utf-8"))
    class_keys = [key for key, spec in legacy_contract["symbols"].items() if spec["kind"] == "class"]

    def distinct_symbols(dictionary, index):
        symbols = dictionary["symbols"]
        for key in class_keys:
            if symbols.get(key):
                name = f"history.unique_{index}.Class_{key}"
                symbols[key] = name
                signature = key[:-4] + "Signature" if key.endswith("Name") else ""
                if signature in symbols:
                    symbols[signature] = "L" + name.replace(".", "/") + ";"

    small.write_text(json.dumps([str(legacy)]), encoding="utf-8")
    paths = []
    for index in range(1200):
        historical = copy.deepcopy(selected_object)
        historical["gameVersion"] = ("1.8.9", "1.20.1", "1.20.4")[index % 3]
        provider = historical["providers"][0]
        provider["family"] = ("Vanilla", "Forge", "Lunar", "Badlion")[index % 4]
        dictionary = provider["dictionaries"][0]
        dictionary["id"] = f"historical-{index}"
        dictionary["family"] = provider["family"]
        distinct_symbols(dictionary, index)
        # History contains real request-amplification inputs rather than 1,200
        # repetitions of the same path or a single unexpanded catalog object.
        path = directory / f"history-{index}.json"
        path.write_text(json.dumps(historical), encoding="utf-8")
        paths.append(str(path))
    history.write_text(json.dumps(paths), encoding="utf-8")

    baseline, baseline_stats, baseline_ms = plan("detection", legacy, small)
    many, many_stats, many_ms = plan("detection", legacy, history)
    absent, absent_stats, absent_ms = plan("detection", legacy, directory / "not-present.json")
    invalid = directory / "invalid-catalog.json"
    invalid.write_text("not JSON and must not be read by independent detection", encoding="utf-8")
    unread, unread_stats, unread_ms = plan("detection", modern, invalid)
    check(baseline == many == absent == unread,
          "detection request depends on historical pack count, pack contents, or unread catalog")
    check(baseline_stats == many_stats == absent_stats == unread_stats,
          "detection request diagnostics change with irrelevant history")
    check(baseline_stats["requestBytes"] < 32768, "detection exceeds unchanged 32KiB request limit")
    check(baseline_stats["configuredRequestBytes"] == 32768, "request-size ceiling was raised")
    check(len(baseline["runtimeAnchors"]) == len(set(baseline["runtimeAnchors"])), "runtime anchors duplicate")
    check(len(baseline["liteClasses"]) == len(set(baseline["liteClasses"])), "detection class scope duplicates")
    check("warmup" not in baseline and not baseline.get("bindingCheck", False),
          "detection unexpectedly hydrates historical classes or requests installed-code proof")
    check(baseline["mode"] == "lite" and len(baseline["liteClasses"]) <= 4,
          "detection unexpectedly expands the game API class scope")

    receipts = []
    for tier, pack, flags in [
        ("vanilla-validation", legacy, ()),
        ("vanilla-validation", modern, ()),
        ("modded-validation", selected, ("--binding-check",)),
        ("modded-validation", legacy, ()),
        ("structural", selected, ()),
    ]:
        one_request, one_stats, elapsed_ms = plan(tier, pack, small, flags)
        history_request, history_stats, _ = plan(tier, pack, history, flags)
        check(one_request == history_request and one_stats == history_stats,
              f"{tier} scope grew from unrelated historical mappings")
        check(one_stats["requestBytes"] < 32768, f"{tier} request exceeds unchanged 32KiB bound")
        hints = [json.dumps(row, sort_keys=True) for row in one_request.get("classHints", [])]
        check(len(hints) == len(set(hints)), f"{tier} classHints duplicate")
        groups = one_request.get("warmup", [])
        check(len(groups) == len({row["anchor"] for row in groups}), f"{tier} warmup repeats an anchor")
        check(all(len(row["classes"]) == len(set(row["classes"])) for row in groups),
              f"{tier} warmup repeats classes within an anchor")
        if tier.endswith("validation"):
            classes = set(one_request["liteClasses"])
            check(len(classes) == len(one_request["liteClasses"]), f"{tier} requested class scope duplicates")
            check(all(set(row["classes"]) <= classes for row in groups), f"{tier} warmup leaves the selected validation scope")
            check(one_request["mode"] == "lite", f"{tier} unexpectedly requests full structures")
            if pack == selected:
                check(len(groups) == 1, "selected cached pack expands another candidate's anchor/warmup")
        receipts.append({"tier": tier, "pack": pack.name, "requestBytes": one_stats["requestBytes"],
                         "requestedClassCount": one_stats["requestedClassCount"],
                         "warmupClassCount": one_stats["warmupClassCount"], "planElapsedMs": elapsed_ms})

    # An explicit selected pack still must be read/validated: ignoring history
    # must never turn into ignoring the candidate mapping itself.
    invalid_selected = subprocess.run([str(analyzer), "inspect", "--plan-capture", "--capture-tier",
        "modded-validation", "--pack", str(invalid), "--contracts", str(contracts)],
        text=True, encoding="utf-8", capture_output=True)
    check(invalid_selected.returncode != 0, "invalid selected candidate pack was ignored")

    oversized = copy.deepcopy(selected_object)
    providers = []
    # Stay within the real schema's 16-dictionary/provider capacity so this
    # exercises request size rather than merely tripping the pack parser.
    for group in range(5):
        provider = copy.deepcopy(selected_object["providers"][0])
        provider["id"] = f"oversized-provider-{group}"
        dictionaries = []
        for offset in range(16):
            index = group * 16 + offset
            dictionary = copy.deepcopy(selected_object["providers"][0]["dictionaries"][0])
            dictionary["id"] = f"oversized-{index}"
            distinct_symbols(dictionary, index)
            dictionaries.append(dictionary)
        provider["dictionaries"] = dictionaries
        providers.append(provider)
    oversized["providers"] = providers
    oversized_path = directory / "genuinely-oversized-selected-pack.json"
    oversized_path.write_text(json.dumps(oversized), encoding="utf-8")
    limit_result = subprocess.run([str(analyzer), "inspect", "--plan-capture", "--capture-tier",
        "modded-validation", "--pack", str(oversized_path), "--contracts", str(contracts)],
        text=True, encoding="utf-8", capture_output=True)
    check(limit_result.returncode != 0 and "probe-request-json" in limit_result.stdout and "32768" in limit_result.stdout,
          "genuinely oversized chosen pack lacks explicit unchanged-limit diagnosis: " + limit_result.stdout + limit_result.stderr)
    print(json.dumps({"test": "offline-request-planner-stress", "historyPacks": 1200, "checks": checks,
        "detection": {"requestBytes": baseline_stats["requestBytes"],
            "requestedClassCount": baseline_stats["requestedClassCount"],
            "warmupClassCount": baseline_stats["warmupClassCount"],
            "planElapsedMs": [baseline_ms, many_ms, absent_ms, unread_ms]},
        "validationAndStructuralPlans": receipts, "jvmAttached": False}, ensure_ascii=False))
