"""Cross-version detection with production Analyzer and synthetic metadata.

Usage: Test-RuntimeDetection.py BUILD [--recorded-runs DIRECTORY]

Recorded inputs are read unchanged and have no invented brand/version evidence.
They prove offline replay only. Synthetic metadata does not prove JNI, Attach,
renderer readiness, or compatibility inside a running Minecraft client.
"""
import argparse
import copy
import hashlib
import json
import re
import subprocess
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument("--recorded-runs", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
build = args.build.resolve()
folder = build / "runtime-detection-fixtures"
folder.mkdir(exist_ok=True)
analyzer = build / "MappingAnalyzer.exe"
contracts = root / "mapping/contracts-v2.json"
legacy_path = root / "mapping/packs/default-v2.json"
modern_path = root / "mapping/packs/vanilla-1.20.1.json"
legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
modern = json.loads(modern_path.read_text(encoding="utf-8"))
bundle = json.loads(contracts.read_text(encoding="utf-8"))
checks = 0
recorded_checks = 0


def check(condition, message):
    global checks
    assert condition, message
    checks += 1


def write(name, value):
    path = folder / name
    path.write_text(json.dumps(value, separators=(",", ":")), encoding="utf-8")
    return path


catalog = write("catalog.json", {"packs": [str(legacy_path), str(modern_path)]})


def run(command, *, allowed=(0,)):
    result = subprocess.run(
        [str(analyzer), *map(str, command)], capture_output=True, text=True, timeout=120
    )
    check(result.returncode in allowed,
          (command, result.returncode, result.stdout[-3500:], result.stderr[-1500:]))
    return result


def signature(name):
    return "L" + name.replace(".", "/") + ";"


def dictionary(pack, family, suffix=None):
    return next(d for p in pack["providers"] for d in p["dictionaries"]
                if d["family"] == family and (suffix is None or d["id"].endswith(suffix)))


def metadata(pack, selected, *, brand=None, launch=None):
    """Authored members become synthetic metadata; no bytecode inference involved."""
    symbols = selected["symbols"]
    contract = bundle["versions"][pack["gameVersion"]]
    classes = {}

    def klass(name):
        return classes.setdefault(name, {
            "name": name, "loader": 1,
            "super": {"name": "Ljava/lang/Object;", "loader": 0},
            "interfaces": [], "fields": [], "methods": [],
            "modifiers": 0, "major": 52 if pack["gameVersion"] == "1.8.9" else 61,
            "minor": 0,
        })

    def descriptor(value):
        return re.sub(r"\{(\w+)\}", lambda match: symbols[match[1]], value)

    for key, spec in contract["symbols"].items():
        value = symbols[key]
        if spec["kind"] == "class" and value:
            klass(signature(value))
        if spec["kind"] not in ("field", "method") or not symbols[spec["owner"]]:
            continue
        owner = klass(signature(symbols[spec["owner"]]))
        names = value if isinstance(value, list) else [value]
        for name in names:
            if not name:
                continue
            member = {"name": name, "descriptor": descriptor(spec["descriptor"]),
                      "modifiers": 8 if spec["static"] else 0}
            members = owner["fields" if spec["kind"] == "field" else "methods"]
            if member not in members:
                members.append(member)
    for ctor in contract["constructors"]:
        if symbols[ctor["owner"]]:
            klass(signature(symbols[ctor["owner"]]))["methods"].append({
                "name": "<init>", "descriptor": descriptor(ctor["descriptor"]), "modifiers": 0})
    if pack["gameVersion"] == "1.20.1":
        for child, parent in (("playerName", "entityPlayerName"),
                              ("entityPlayerName", "livingName"),
                              ("livingName", "entityName"),
                              ("worldClientName", "worldName")):
            klass(signature(symbols[child]))["super"] = {
                "name": signature(symbols[parent]), "loader": 1}
    result = {
        "snapshotVersion": 1, "complete": True,
        "captureKind": "jvmti-metadata-double-read", "detailLevel": "lite",
        "pid": 7, "processStart": "synthetic-runtime-detection", "requestId": "fixture",
        "loaders": [{"id": 1, "type": "LSyntheticFixtureLoader;", "instance": 101}],
        "classes": list(classes.values()), "launchEvidence": launch or [],
    }
    if brand is not None:
        result["runtimeEvidence"] = [{
            "evidenceVersion": 1, "anchor": symbols["minecraftSignature"], "loader": 1,
            "version": pack["gameVersion"], "versionSource": "version.json",
            "brand": brand, "brandSource": "ClientBrandRetriever",
        }]
    return result


def normalize(name, raw):
    snapshot = folder / (name + ".jsonl")
    run(["inspect", "--snapshot", write(name + "-raw.json", raw), "--out", snapshot])
    return snapshot


def identify(name, snapshot, allowed=(0,)):
    output = folder / (name + "-identity.json")
    result = run(["identify", "--pack", legacy_path, "--identity-packs", catalog,
                  "--contracts", contracts, "--snapshot", snapshot, "--out", output],
                 allowed=allowed)
    return json.loads(output.read_text()) if output.exists() else {}, result


def authored(name, snapshot, pack, allowed=(0, 3)):
    output = folder / (name + "-authored.json")
    run(["validate-authored", "--pack", pack, "--contracts", contracts,
         "--snapshot", snapshot, "--out", output], allowed=allowed)
    return json.loads(output.read_text())


vanilla_dictionary = dictionary(legacy, "Vanilla")
modern_dictionary = dictionary(modern, "Vanilla")
legacy_raw = metadata(legacy, vanilla_dictionary, brand="vanilla")
modern_raw = metadata(modern, modern_dictionary, brand="vanilla")

# Identical names across Vanilla/Lunar never constitute Lunar identity evidence.
legacy_snapshot = normalize("vanilla-189", legacy_raw)
identity, _ = identify("vanilla-189", legacy_snapshot)
check(identity["minecraftVersion"] == "1.8.9" and identity["versionVerified"], identity)
check(identity["family"] == "Vanilla" and identity["mappingNamespace"] == "Notch", identity)
check(Path(identity["selectedPack"]).resolve() == legacy_path.resolve(), identity)
receipt = authored("vanilla-189", legacy_snapshot, legacy_path)
check(receipt["valid"] and receipt["injectionReady"] and
      receipt["dictionary"] == vanilla_dictionary["id"], receipt)
check(receipt["validationMode"] == "authored-metadata" and
      not receipt["identity"]["bindingIdentity"], receipt)

# Modern DataFixer Lave is an old anchor-name collision, not a second MC version.
collision = {"name": "Lave;", "loader": 1,
             "super": {"name": "Ljava/lang/Object;", "loader": 0},
             "interfaces": [], "fields": [], "modifiers": 0, "major": 61, "minor": 0,
             "methods": [{"name": "a", "descriptor":
                          "(Lcom/mojang/datafixers/Typed;)Lcom/mojang/datafixers/Typed;",
                          "modifiers": 0}]}
modern_raw["classes"].append(collision)
modern_snapshot = normalize("vanilla-1201-collision", modern_raw)
identity, _ = identify("vanilla-1201-collision", modern_snapshot)
check(identity["minecraftVersion"] == "1.20.1" and identity["versionVerified"], identity)
check(identity["family"] == "Vanilla" and identity["mappingNamespace"] == "Notch", identity)
check(Path(identity["selectedPack"]).resolve() == modern_path.resolve(), identity)
receipt = authored("vanilla-1201-collision", modern_snapshot, modern_path)
check(receipt["valid"] and receipt["injectionReady"] and
      receipt["dictionary"] == modern_dictionary["id"], receipt)
check(receipt["validationMode"] == "authored-metadata" and
      not receipt["identity"]["bindingIdentity"], receipt)

for name, raw, expected in (("unknown-189", legacy_raw, "1.8.9"),
                            ("unknown-1201-collision", modern_raw, "1.20.1")):
    unknown = copy.deepcopy(raw)
    unknown.pop("runtimeEvidence")
    snapshot = normalize(name, unknown)
    identity, _ = identify(name, snapshot)
    check(identity["family"] == "Unknown" and not identity["versionVerified"], identity)
    check(identity["minecraftVersion"] == expected, identity)
    receipt = authored(name, snapshot, legacy_path if expected == "1.8.9" else modern_path)
    check(not receipt["injectionReady"], receipt)

# Modified clients retain their independent family and authored namespace.
for family, selected, namespace in (
        ("Forge", dictionary(legacy, "Forge"), "SRG"),
        ("Lunar", dictionary(legacy, "Lunar", "-notch"), "Notch")):
    raw = metadata(legacy, selected, brand=family.lower(),
                   launch=[{"family": family, "confidence": 250}])
    identity, _ = identify(family.lower() + "-189", normalize(family.lower() + "-189", raw))
    check(identity["family"] == family and identity["minecraftVersion"] == "1.8.9", identity)
    check(identity["versionVerified"], identity)
    check(identity["mappingNamespace"] == namespace, identity)

# Official legacy Forge emits exactly fml,forge. An unaudited additional token
# cannot be accepted just because its text contains the known forge substring.
canonical_forge = metadata(legacy, dictionary(legacy, "Forge"), brand="fml,forge",
                           launch=[{"family": "Forge", "confidence": 250}])
identity, _ = identify("forge-canonical-brand", normalize("forge-canonical-brand", canonical_forge))
check(identity["family"] == "Forge" and identity["minecraftVersion"] == "1.8.9" and
      identity["versionVerified"] and identity["mappingNamespace"] == "SRG", identity)
unrecognized_forge = copy.deepcopy(canonical_forge)
unrecognized_forge["runtimeEvidence"][0]["brand"] = "fml,forge,unknown"
unrecognized_snapshot = normalize("forge-extra-brand-token", unrecognized_forge)
identity, _ = identify("forge-extra-brand-token", unrecognized_snapshot)
check(identity["family"] == "Unknown" and identity["versionVerified"], identity)
check(not authored("forge-extra-brand-token", unrecognized_snapshot, legacy_path)["injectionReady"], identity)

# A class-marker receipt belongs to its defining loader. An unrelated loader
# must not turn positive Vanilla into Lunar, or override current Forge brand.
def lunar_marker(raw, loader):
    result = copy.deepcopy(raw)
    if loader == 2:
        result["loaders"].append({"id": 2, "type": "LUnrelatedMarkerLoader;", "instance": 202})
    result["classes"].append({
        "name": "Lcom/lunarclient/UnrelatedMarker;", "loader": loader,
        "super": {"name": "Ljava/lang/Object;", "loader": 0},
        "interfaces": [], "fields": [], "methods": [], "modifiers": 0,
        "major": 52, "minor": 0,
    })
    result["launchEvidence"] = [{"family": "Lunar", "confidence": 250, "loader": loader}]
    return result


foreign_vanilla = normalize("vanilla-foreign-lunar", lunar_marker(legacy_raw, 2))
identity, _ = identify("vanilla-foreign-lunar", foreign_vanilla)
check(identity["family"] == "Unknown" and identity["versionVerified"], identity)
check(not authored("vanilla-foreign-lunar", foreign_vanilla, legacy_path)["injectionReady"], identity)

forge_current = metadata(legacy, dictionary(legacy, "Forge"), brand="forge")
identity, _ = identify("forge-foreign-lunar", normalize("forge-foreign-lunar", lunar_marker(forge_current, 2)))
check(identity["family"] == "Forge" and identity["minecraftVersion"] == "1.8.9", identity)
check(identity["mappingNamespace"] == "SRG", identity)

identity, _ = identify("same-loader-lunar", normalize("same-loader-lunar", lunar_marker(legacy_raw, 1)))
check(identity["family"] == "Lunar" and identity["minecraftVersion"] == "1.8.9", identity)
check(identity["mappingNamespace"] == "Notch", identity)

# The observed Lunar release brand is client identity only, combined with an
# independently scoped strong marker. Its v2.23.4 release is never an MC version.
for index, observed in enumerate(("lunarclient:v2.23.4-2642,fml,forge", "lunarclient:v2.23.4-2642")):
    raw = lunar_marker(legacy_raw, 1)
    raw["runtimeEvidence"][0].update(brand="modified", brandObserved=observed)
    name = f"audited-lunar-brand-{index}"
    identity, _ = identify(name, normalize(name, raw))
    check(identity["family"] == "Lunar" and identity["minecraftVersion"] == "1.8.9" and
          identity["versionVerified"] and identity["mappingNamespace"] == "Notch", identity)
for index, observed in enumerate(("lunarclient:v2.23.4-2642,fml,forge,unknown",
                                  "lunarclient:v2.23-2642,fml,forge",
                                  "lunarclient:v2.23.4-2642,unknown")):
    raw = lunar_marker(legacy_raw, 1)
    raw["runtimeEvidence"][0].update(brand="modified", brandObserved=observed)
    name = f"unaudited-lunar-brand-{index}"
    snapshot = normalize(name, raw)
    identity, _ = identify(name, snapshot)
    check(identity["family"] == "Unknown" and identity["versionVerified"], identity)
    check(not authored(name, snapshot, legacy_path)["injectionReady"], identity)
for name, marker_loader in (("audited-brand-foreign-marker", 2), ("audited-brand-no-marker", None)):
    raw = lunar_marker(legacy_raw, marker_loader) if marker_loader else copy.deepcopy(legacy_raw)
    raw["runtimeEvidence"][0].update(brand="modified", brandObserved="lunarclient:v2.23.4-2642,fml,forge")
    identity, _ = identify(name, normalize(name, raw))
    check(identity["family"] == "Unknown" and identity["versionVerified"], identity)
weak_marker = copy.deepcopy(legacy_raw)
weak_marker["runtimeEvidence"][0].update(brand="modified", brandObserved="lunarclient:v2.23.4-2642,fml,forge")
weak_marker["launchEvidence"] = [{"family": "Lunar", "confidence": 199, "loader": 1}]
identity, _ = identify("audited-brand-weak-marker", normalize("audited-brand-weak-marker", weak_marker))
check(identity["family"] == "Unknown" and identity["versionVerified"], identity)

# Detection intentionally contains no API members or hierarchy. Independent
# loader evidence may choose a candidate, but it cannot validate that mapping.
def detection_only(raw, extra_anchor=None):
    result = copy.deepcopy(raw)
    anchors = {item["anchor"] for item in result["runtimeEvidence"]}
    result["classes"] = [item for item in result["classes"] if item["name"] in anchors]
    for item in result["classes"]:
        item.update(fields=[], methods=[], interfaces=[],
                    super={"name": "", "loader": 0}, modifiers=0)
    result["captureTier"] = "detection"
    if extra_anchor:
        stub = copy.deepcopy(result["classes"][0])
        stub["name"] = extra_anchor
        result["classes"].append(stub)
        receipt = copy.deepcopy(result["runtimeEvidence"][0])
        receipt["anchor"] = extra_anchor
        result["runtimeEvidence"].append(receipt)
    return result


minimal_modern = detection_only(modern_raw, "Lnet/minecraft/client/main/Main;")
minimal_snapshot = normalize("minimal-modern-detection", minimal_modern)
identity, _ = identify("minimal-modern-detection", minimal_snapshot)
check(identity["family"] == "Vanilla" and identity["minecraftVersion"] == "1.20.1" and
      identity["versionVerified"], identity)
check(Path(identity["selectedPack"]).resolve() == modern_path.resolve() and
      identity["mappingNamespace"] == "Notch", identity)
check(not authored("minimal-modern-detection", minimal_snapshot, modern_path)["injectionReady"], identity)

main_only = copy.deepcopy(minimal_modern)
main_only["classes"] = [item for item in main_only["classes"]
                        if item["name"] == "Lnet/minecraft/client/main/Main;"]
main_only["runtimeEvidence"] = [item for item in main_only["runtimeEvidence"]
                                if item["anchor"] == "Lnet/minecraft/client/main/Main;"]
main_snapshot = normalize("main-only-detection", main_only)
identity, _ = identify("main-only-detection", main_snapshot)
check(identity["family"] == "Vanilla" and identity["minecraftVersion"] == "1.20.1" and
      identity["versionVerified"], identity)
check(not identity.get("selectedPack") and identity["mappingNamespace"] == "Unknown", identity)
check(not authored("main-only-detection", main_snapshot, modern_path)["injectionReady"], identity)

# Runtime version authority is not the version of the closest installed pack.
forge_modern = copy.deepcopy(modern_raw)
forge_anchor = "Lnet/minecraft/client/Minecraft;"
modern_anchor = modern_dictionary["symbols"]["minecraftSignature"]
for klass in forge_modern["classes"]:
    if klass["name"] == modern_anchor:
        klass["name"] = forge_anchor
        for member in klass["methods"] + klass["fields"]:
            member["name"] = "modern_forge_" + member["name"]
forge_modern["runtimeEvidence"][0].update(anchor=forge_anchor, brand="forge")
forge_modern["launchEvidence"] = [{"family": "Forge", "confidence": 250}]
snapshot = normalize("forge-1201", forge_modern)
identity, _ = identify("forge-1201", snapshot)
check(identity["family"] == "Forge" and identity["minecraftVersion"] == "1.20.1", identity)
check(identity["versionVerified"] and not identity.get("selectedPack"), identity)
check(not authored("forge-1201", snapshot, modern_path)["injectionReady"], identity)
identity, _ = identify("minimal-forge-1201", normalize("minimal-forge-1201", detection_only(forge_modern)))
check(identity["family"] == "Forge" and identity["minecraftVersion"] == "1.20.1" and
      identity["versionVerified"], identity)
check(not identity.get("selectedPack") and identity["mappingNamespace"] == "Unknown", identity)

# A known modified family with one unverified candidate loader may request one
# bounded independent-evidence pass. That receipt is scope, never a Ready proof.
recovery_raw = detection_only(metadata(legacy, dictionary(legacy, "Lunar", "-mcp"),
                                      brand="lunar", launch=[{"family": "Lunar", "confidence": 250}]))
for item in recovery_raw["runtimeEvidence"]:
    item.update(version="", versionSource="", diagnostic="independent runtime version evidence unavailable")
recovery_snapshot = normalize("modified-candidate-recovery", recovery_raw)
identity, _ = identify("modified-candidate-recovery", recovery_snapshot)
recovery = identity.get("evidenceRecovery", {})
check(recovery.get("eligible") and recovery["family"] == "Lunar" and recovery["versions"] == ["1.8.9"], identity)
check(recovery["runtimeBinding"]["type"] == "LSyntheticFixtureLoader;" and
      recovery["runtimeBinding"]["instance"] == 101, identity)
check(not identity["versionVerified"] and not identity["minecraftVersion"] and
      not identity.get("selectedPack") and not identity.get("runtimeBinding") and
      identity["mappingNamespace"] == "Unknown", identity)
check(not authored("modified-candidate-recovery", recovery_snapshot, legacy_path)["injectionReady"], identity)

unknown_recovery = copy.deepcopy(recovery_raw)
unknown_recovery["launchEvidence"] = []
unknown_recovery["runtimeEvidence"][0].update(brand="", brandSource="")
identity, _ = identify("unknown-candidate-recovery", normalize("unknown-candidate-recovery", unknown_recovery))
check(not identity.get("evidenceRecovery", {}).get("eligible") and not identity["versionVerified"], identity)

conflicted_recovery = copy.deepcopy(recovery_raw)
conflicted_recovery["runtimeEvidence"][0].update(diagnostic="conflicting runtime version evidence", attempts=[
    {"source": "loader-version-json", "status": "verified-value", "value": "1.20.1", "observed": "1.20.1", "reason": ""},
    {"source": "realms-shared-constants", "status": "verified-value", "value": "1.8.9", "observed": "1.8.9", "reason": ""},
])
identity, _ = identify("conflicting-candidate-recovery", normalize("conflicting-candidate-recovery", conflicted_recovery))
check(not identity.get("evidenceRecovery", {}).get("eligible") and not identity["versionVerified"], identity)

ambiguous_recovery = copy.deepcopy(recovery_raw)
ambiguous_recovery["loaders"].append({"id": 2, "type": "LOtherCandidateLoader;", "instance": 202})
duplicate_classes = copy.deepcopy(ambiguous_recovery["classes"])
for item in duplicate_classes:
    item["loader"] = 2
ambiguous_recovery["classes"].extend(duplicate_classes)
duplicate_evidence = copy.deepcopy(ambiguous_recovery["runtimeEvidence"][0])
duplicate_evidence["loader"] = 2
ambiguous_recovery["runtimeEvidence"].append(duplicate_evidence)
identity, _ = identify("multiple-candidate-loaders", normalize("multiple-candidate-loaders", ambiguous_recovery))
check(not identity.get("evidenceRecovery", {}).get("eligible") and not identity["versionVerified"], identity)

# Even a successfully observed semantic version candidate does not supply the
# API classes/members omitted by Detection; authored validation is still required.
proved_recovery = copy.deepcopy(recovery_raw)
proved_recovery["runtimeEvidence"][0].update(version="1.8.9", versionSource="realms-semantic-candidate",
                                           diagnostic="unusable version.json ignored; unsupported runtime version resource id")
proved_snapshot = normalize("semantic-candidate-detection", proved_recovery)
identity, _ = identify("semantic-candidate-detection", proved_snapshot)
check(identity["family"] == "Lunar" and identity["minecraftVersion"] == "1.8.9" and
      identity["versionVerified"] and identity["mappingNamespace"] == "MCP", identity)
check(not authored("semantic-candidate-detection", proved_snapshot, legacy_path)["injectionReady"], identity)

unsupported = copy.deepcopy(modern_raw)
unsupported["runtimeEvidence"][0]["version"] = "1.20.4"
identity, _ = identify("unsupported-1204", normalize("unsupported-1204", unsupported))
check(identity["minecraftVersion"] == "1.20.4" and identity["versionVerified"], identity)
check(not identity.get("selectedPack"), identity)

# Evidence belongs to its defining loader; an unrelated loader cannot authorize it.
wrong_loader = copy.deepcopy(modern_raw)
wrong_loader["loaders"].append({"id": 2, "type": "LOtherFixtureLoader;", "instance": 202})
wrong_loader["runtimeEvidence"][0]["loader"] = 2
identity, _ = identify("wrong-evidence-loader", normalize("wrong-evidence-loader", wrong_loader))
check(not identity["versionVerified"] and identity["family"] == "Unknown", identity)
check(not identity.get("evidenceRecovery", {}).get("eligible"), identity)

# An exact version/brand does not bypass the necessary member descriptor checks.
wrong_descriptor = copy.deepcopy(modern_raw)
symbols = modern_dictionary["symbols"]
for klass in wrong_descriptor["classes"]:
    if klass["name"] == symbols["minecraftSignature"]:
        for field in klass["fields"]:
            if field["name"] == symbols["playerField"]:
                field["descriptor"] = "Ljava/lang/Object;"
receipt = authored("wrong-player-descriptor", normalize("wrong-player-descriptor", wrong_descriptor), modern_path)
check(not receipt["injectionReady"], receipt)
rows = list(receipt.get("symbols", []))
for attempt in receipt.get("attempts", []):
    rows.extend(attempt.get("symbols", []))
check(any(row.get("symbol") == "playerField" and not row.get("accepted") for row in rows), receipt)

# Two complete same-name API scopes without a unique live loader fail closed.
two_loaders = copy.deepcopy(modern_raw)
two_loaders.pop("runtimeEvidence")
two_loaders["loaders"].append({"id": 2, "type": "LOtherFixtureLoader;", "instance": 202})
duplicate_classes = copy.deepcopy(two_loaders["classes"])
for klass in duplicate_classes:
    klass["loader"] = 2
    if klass["super"]["loader"] == 1:
        klass["super"]["loader"] = 2
two_loaders["classes"].extend(duplicate_classes)
identity, process = identify("two-complete-loaders", normalize("two-complete-loaders", two_loaders), allowed=(0, 1, 3))
check(process.returncode != 0 or (not identity.get("versionVerified") and
      not identity.get("runtimeBinding")), identity)

# Optional unchanged historical metadata replay. Never inject new authority into it.
recorded = args.recorded_runs
if recorded is None:
    recorded = (Path.home() / "AppData/Local/Overlay Studio/MinecraftOverlayManager/"
                "mapping-cache-v1/runs")
cases = (
    ("recorded-vanilla-189", "beef1e76-6c07-410a-acd9-f5ad9cf6776d", "Unknown", "1.8.9"),
    ("recorded-vanilla-1201", "36ff0828-0084-434a-813e-1db334c3b63a", "Unknown", "1.20.1"),
    ("recorded-forge-1201", "fb9c53cb-053c-42c5-8275-26c16c26eba5", "Forge", None),
    ("recorded-lunar-detection", "068eed5d-71ec-4e1e-9bd0-25d754046947", "Lunar", None),
)
for name, run_id, family, candidate_version in cases:
    source = recorded / run_id / "index-1.jsonl"
    if not source.is_file():
        print("SKIP historical replay (not distributed):", run_id)
        continue
    original_digest = hashlib.sha256(source.read_bytes()).hexdigest()
    identity, _ = identify(name, source)
    check(identity["family"] == family and not identity["versionVerified"], identity)
    if candidate_version is not None:
        check(identity["minecraftVersion"] == candidate_version, identity)
    else:
        check(identity["minecraftVersion"] != "1.8.9", identity)
    check(hashlib.sha256(source.read_bytes()).hexdigest() == original_digest,
          "historical Snapshot must remain unchanged")
    if name == "recorded-lunar-detection":
        check(identity.get("evidenceRecovery", {}).get("eligible") and
              identity["evidenceRecovery"]["versions"] == ["1.8.9"], identity)
        check(not identity.get("selectedPack") and not identity.get("runtimeBinding") and
              identity["mappingNamespace"] == "Unknown", identity)
        recorded_checks += 5
    else:
        recorded_checks += 3

print(f"{checks} runtime detection checks passed; {recorded_checks} assertions used unchanged "
      "historical Minecraft metadata. Remaining checks use synthetic offline metadata; "
      "no running-game Attach or Renderer Ready is claimed.")
