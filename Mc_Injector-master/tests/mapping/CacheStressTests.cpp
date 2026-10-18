#include "../../mapping/cache/MappingCache.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <cstdio>
#include <vector>

using namespace mapping_cache;

// These are cache-integrity fixtures, not Minecraft snapshots or proof that a
// runtime is compatible. Current runtime validation remains the caller's gate.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *description) {
        ++checks;
        if (!ok) { ++failures; std::printf("FAIL %s\n", description); }
    };
    check(temp.isValid(), "temporary cache available");
    if (!temp.isValid()) return 1;
    QElapsedTimer clock;
    clock.start();
    const auto root = temp.path() + "/cache";
    QDir().mkpath(root + "/objects");
    const QString contract = "active-contract", oldContract = "old-contract";
    const QStringList families{"Lunar", "Forge", "Vanilla", "Badlion"};
    const QStringList versions{"1.8.9", "1.20.1", "1.20.4"};
    struct Fixture { QString revision, family, version, identity, snapshot; bool active; int serial; };
    std::vector<Fixture> fixtures;
    QJsonObject verified;
    constexpr int historyCount = 1200;
    for (int i = 0; i < historyCount; ++i) {
        const auto revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto dir = root + "/objects/" + revision;
        QDir().mkpath(dir);
        const auto family = families[i % families.size()];
        const auto version = versions[(i / families.size()) % versions.size()];
        const auto identity = QString("identity-%1").arg(i);
        const bool active = i % 17 != 0;
        writeObject(dir + "/pack.json", {
            {"gameVersion", version}, {"providers", QJsonArray{QJsonObject{
                {"dictionaries", QJsonArray{QJsonObject{{"family", family}, {"fixture", i}}}}}}}});
        writeObject(dir + "/snapshot.json", {{"fixture", "cache-integrity-only"}, {"revision", i}});
        writeObject(dir + "/proof.json", {
            {"validated", true}, {"analyzerVersion", 5}, {"contractDigest", active ? contract : oldContract},
            {"packDigest", fileDigest(dir + "/pack.json")}, {"snapshotDigest", fileDigest(dir + "/snapshot.json")},
            {"mappingIdentity", identity}, {"metadataIdentity", QString("metadata-%1").arg(i)},
            {"family", family}, {"minecraftVersion", version}, {"fingerprint", QString("snapshot-%1").arg(i)},
            {"promotionSerial", i + 1}, {"bindingIdentity", QString("binding-%1").arg(i)}});
        verified[identity] = revision;
        // Old indexes can contain more than one stable key for a revision.
        // Aliases must not amplify candidate lists or history-derived scopes.
        if (i % 8 == 0) verified[QString("alias-%1").arg(i)] = revision;
        fixtures.push_back({revision, family, version, identity, dir + "/snapshot.json", active, i + 1});
    }
    writeObject(root + "/index.json", {{"cacheVersion", 2}, {"verified", verified},
        {"lastVerified", fixtures.back().revision}, {"lastFingerprint", fixtures.back().identity}});
    Cache cache(root);
    int activeCount = 0, matchingCount = 0;
    const Fixture *newest = nullptr;
    for (const auto &f : fixtures) {
        if (f.active) ++activeCount;
        if (f.active && f.family == "Lunar" && f.version == "1.8.9") { ++matchingCount; newest = &f; }
    }
    const auto known = cache.knownPacks(contract);
    check(known.size() == activeCount, "1200-object history enumerates every compatible revision once without an arbitrary cap");
    check(QSet<QString>(known.begin(), known.end()).size() == known.size(), "knownPacks deduplicates index aliases");
    check(cache.knownPacks("missing-contract").isEmpty(), "history never crosses contract digests");
    const auto candidates = cache.candidates("Lunar", "1.8.9", contract);
    check(candidates.size() == matchingCount, "candidate set includes all and only exact family/version history");
    QSet<QString> revisions;
    bool scoped = true, ordered = true;
    quint64 previousSerial = ~quint64(0);
    for (const auto &entry : candidates) {
        revisions.insert(entry.revision);
        scoped &= entry.family == "Lunar" && entry.minecraftVersion == "1.8.9" && entry.contractDigest == contract;
        ordered &= entry.promotionSerial <= previousSerial;
        previousSerial = entry.promotionSerial;
    }
    check(scoped && revisions.size() == candidates.size(), "aliases and other families/versions cannot bind a selected candidate");
    check(ordered && newest && candidates.front().revision == newest->revision, "compatible history remains ordered by latest promotion");
    check(cache.candidates("Unknown", "1.8.9", contract).isEmpty(), "Unknown family has no reusable candidate");
    check(cache.candidates("Lunar", {}, contract).isEmpty(), "unknown version has no reusable candidate");
    check(cache.candidates("Lunar", "1.21.0", contract).isEmpty(), "unsupported API version does not reuse older mappings");
    const auto reference = cache.reference(contract, "Lunar", "1.8.9");
    check(reference.valid() && newest && reference.revision == newest->revision &&
        reference.referenceReason == "same-family-and-minecraft-version", "structural reference stays within the same API contract");
    const auto exact = candidates.back();
    check(cache.reference(contract, "Lunar", "1.8.9", exact.mappingIdentity).revision == exact.revision,
        "exact stable identity outranks newer compatible references");
    check(!cache.reference(contract, "Lunar", "1.21.0").valid(), "same-family nearest OTHER version is not a structural reference");
    check(!cache.reference(contract, "Unknown", "1.8.9").valid() && !cache.reference(contract, "Lunar", {}).valid(),
        "structural reference requires independent family and version");
    const auto base = cache.reference(contract, "ExplicitModdedFixture", "1.20.1");
    check(base.valid() && base.family == "Vanilla" && base.minecraftVersion == "1.20.1" &&
        base.referenceReason == "explicit-base-minecraft-structural-reference", "explicit Vanilla base is available only for the exact target version");
    check(!cache.reference(contract, "ExplicitModdedFixture", "1.21.0").valid(), "Vanilla base does not cross versions");
    const auto otherVersion = cache.candidates("Lunar", "1.20.1", contract).front();
    const auto noCrossIdentity = cache.reference(contract, "Lunar", "1.8.9", otherVersion.mappingIdentity);
    check(noCrossIdentity.valid() && noCrossIdentity.minecraftVersion == "1.8.9" &&
        noCrossIdentity.mappingIdentity != otherVersion.mappingIdentity, "an identity hint never overrides the actual target API version");
    check(cache.preferred(contract).valid() && cache.preferred(contract).revision == fixtures.back().revision,
        "lastVerified remains an ordering hint rather than an independently compatible candidate");

    // Fast enumeration deliberately does not read every historical snapshot;
    // selecting a structural reference must still verify the selected bytes.
    writeObject(newest->snapshot, {{"tampered", true}});
    check(cache.candidates("Lunar", "1.8.9", contract).size() == matchingCount,
        "candidate enumeration does not turn into a full historical snapshot scan");
    const auto fallback = cache.reference(contract, "Lunar", "1.8.9");
    check(fallback.valid() && fallback.revision != newest->revision && fallback.minecraftVersion == "1.8.9",
        "corrupt selected snapshot falls through only to an integrity-checked compatible reference");
    const auto packPath = candidates.front().pack;
    writeObject(packPath, {{"tampered", true}});
    check(cache.candidates("Lunar", "1.8.9", contract).size() == matchingCount - 1,
        "pack integrity is checked for every enumerated live candidate");
    check(!cache.lookup(newest->identity, contract).valid(), "tampered source cannot become a lookup hit");
    auto index = readObject(root + "/index.json");
    index["lastVerified"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    writeObject(root + "/index.json", index);
    check(!cache.preferred(contract).valid(), "unindexed lastVerified does not become cache authority");
    std::printf("CACHE_STRESS objects=%d indexEntries=%lld knownPacks=%lld exactCandidates=%lld elapsedMs=%lld checks=%d failures=%d\n",
        historyCount, static_cast<long long>(verified.size()), static_cast<long long>(known.size()),
        static_cast<long long>(candidates.size()), static_cast<long long>(clock.elapsed()), checks, failures);
    return failures ? 1 : 0;
}
