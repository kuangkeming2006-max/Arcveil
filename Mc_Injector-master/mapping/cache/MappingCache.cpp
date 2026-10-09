#include "MappingCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>
#include <algorithm>
#include <vector>
namespace mapping_cache {
QJsonObject readObject(const QString &path, qint64 limit) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > limit)
        throw std::runtime_error("missing/oversize mapping JSON");
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error("invalid mapping JSON");
    return doc.object();
}
void writeObject(const QString &path, const QJsonObject &value) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error("mapping output unavailable");
    auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (f.write(bytes) != bytes.size() || !f.commit())
        throw std::runtime_error("atomic mapping write failed");
}
QString fileDigest(const QString &path, qint64 limit) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || (limit >= 0 && f.size() > limit))
        throw std::runtime_error("mapping digest input unavailable or exceeds explicit limit");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        throw std::runtime_error("mapping digest read failed");
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject Cache::index() const {
    if (!QFile::exists(m_root + "/index.json"))
        return {{"cacheVersion", 1}, {"verified", QJsonObject{}}, {"previous", QJsonObject{}}};
    auto value = readObject(m_root + "/index.json");
    if (value.value("cacheVersion").toInt() != 1 && value.value("cacheVersion").toInt() != 2)
        throw std::runtime_error("unsupported mapping cache version");
    return value;
}
Entry Cache::entry(const QString &revision, const QString &contractDigest, bool verifySnapshot) const {
    if (!QRegularExpression("^[a-f0-9-]{36}$").match(revision).hasMatch())
        return {};
    try {
        const auto dir = m_root + "/objects/" + revision;
        const auto proof = readObject(dir + "/proof.json");
        if (!proof.value("validated").toBool() ||
            proof.value("contractDigest").toString() != contractDigest ||
            (proof.value("analyzerVersion").toInt() != 4 && proof.value("analyzerVersion").toInt() != 5))
            return {};
        if (fileDigest(dir + "/pack.json", 2 * 1024 * 1024) !=
                proof.value("packDigest").toString() ||
            (verifySnapshot && fileDigest(dir + "/snapshot.json") != proof.value("snapshotDigest").toString()))
            return {};
        auto family = proof.value("family").toString();
        auto version = proof.value("minecraftVersion").toString();
        if (family.isEmpty()) {
            const auto pack = readObject(dir + "/pack.json");
            const auto providers = pack.value("providers").toArray();
            if (providers.size() == 1) {
                const auto dictionaries = providers[0].toObject().value("dictionaries").toArray();
                if (dictionaries.size() == 1) family = dictionaries[0].toObject().value("family").toString();
            }
            version = pack.value("gameVersion").toString();
        }
        return {revision,
                proof.value("fingerprint").toString(),
                dir + "/pack.json",
                dir + "/snapshot.json",
                proof.value("packDigest").toString(),
                contractDigest,
                proof.value("mappingIdentity").toString(), proof.value("metadataIdentity").toString(),
                family, version, {}, quint64(proof.value("promotionSerial").toDouble()), proof.value("bindingIdentity").toString()};
    } catch (...) {
        return {};
    }
}
QStringList Cache::knownPacks(const QString &contractDigest) const {
    QStringList result;
    const auto verified = index().value("verified").toObject();
    for (auto it = verified.begin(); it != verified.end(); ++it) {
        const auto e = entry(it.value().toString(), contractDigest, false);
        if (e.valid()) result << e.pack;
    }
    return result;
}
Entry Cache::preferred(const QString &contractDigest) const {
    // Ordering hint only. Caller must independently check current family, complete
    // required metadata/content and defining loader before authorizing reuse.
    const auto i = index();
    const auto revision = i.value("lastVerified").toString();
    bool indexed = false;
    const auto verified = i.value("verified").toObject();
    for (auto it = verified.begin(); it != verified.end(); ++it) indexed |= it.value().toString() == revision;
    if (!indexed) return {};
    auto e = entry(revision, contractDigest, false);
    return e.valid() && !e.mappingIdentity.isEmpty() && !e.metadataIdentity.isEmpty() ? e : Entry{};
}
Entry Cache::lookup(const QString &fingerprint, const QString &contractDigest) const {
    auto e =
        entry(index().value("verified").toObject().value(fingerprint).toString(), contractDigest);
    return e.mappingIdentity == fingerprint || (e.mappingIdentity.isEmpty() && e.fingerprint == fingerprint) ? e : Entry{};
}
QList<Entry> Cache::candidates(const QString &family, const QString &minecraftVersion,
                               const QString &contractDigest) const {
    QList<Entry> result;
    if (family.isEmpty() || family == "Unknown") return result;
    const auto i = index();
    const auto verified = i.value("verified").toObject();
    // Last is only an ordering hint within an already family-scoped set.
    const auto last = i.value("lastVerified").toString();
    for (auto it = verified.begin(); it != verified.end(); ++it) {
        auto e = entry(it.value().toString(), contractDigest, false);
        if (!e.valid() || e.family != family || e.minecraftVersion != minecraftVersion ||
            e.mappingIdentity.isEmpty()) continue;
        if (e.revision == last) result.prepend(e); else result.append(e);
    }
    std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b) { return a.promotionSerial > b.promotionSerial; });
    return result;
}
Entry Cache::reference(const QString &contractDigest, const QString &family,
                       const QString &minecraftVersion, const QString &mappingIdentity) const {
    if (family.isEmpty() || family == "Unknown") return {};
    struct RankedReference { Entry value; int rank, distance; };
    std::vector<RankedReference> references;
    auto distance = [&](const QString &version) {
        const auto source = version.split('.'), target = minecraftVersion.split('.');
        int result = 0;
        for (int i = 0; i < 3; ++i) {
            bool a = false, b = false;
            const int x = source.value(i).toInt(&a), y = target.value(i).toInt(&b);
            if (!a || !b) return version == minecraftVersion ? 0 : 1000000;
            result += qAbs(x - y) * (i == 0 ? 10000 : i == 1 ? 100 : 1);
        }
        return result;
    };
    const auto verified = index().value("verified").toObject();
    for (auto it = verified.begin(); it != verified.end(); ++it) {
        auto e = entry(it.value().toString(), contractDigest, false);
        if (!e.valid()) continue;
        int rank = 0;
        QString reason;
        if (e.family == family) {
            if (!mappingIdentity.isEmpty() && e.mappingIdentity == mappingIdentity) {
                rank = 4; reason = "exact-stable-mapping-identity";
            } else if (e.minecraftVersion == minecraftVersion) {
                rank = 3; reason = "same-family-and-minecraft-version";
            } else { rank = 2; reason = "same-family-nearest-verified-reference"; }
        } else if (e.family == "Vanilla" && e.minecraftVersion == minecraftVersion) {
            rank = 1; reason = "explicit-base-minecraft-structural-reference";
        }
        const int separation = distance(e.minecraftVersion);
        if (rank > 0) { e.referenceReason = reason; references.push_back({e, rank, separation}); }
    }
    std::sort(references.begin(), references.end(), [](const auto &a, const auto &b) {
        if (a.rank != b.rank) return a.rank > b.rank;
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.value.promotionSerial > b.value.promotionSerial;
    });
    // Historical snapshots are only read when selecting a structural reference.
    // Reusable candidates authorize nothing until their pack passes current live validation.
    for (const auto &candidate : references) {
        auto verified = entry(candidate.value.revision, contractDigest, true);
        if (verified.valid()) { verified.referenceReason = candidate.value.referenceReason; return verified; }
    }
    return {};
}
void Cache::candidate(const QString &run, const QString &fingerprint, const QString &status) {
    QDir().mkpath(m_root);
    QLockFile lock(m_root + "/cache.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        throw std::runtime_error("mapping cache is busy");
    auto i = index();
    i["candidate"] = QJsonObject{{"run", run}, {"fingerprint", fingerprint}, {"status", status}};
    writeObject(m_root + "/index.json", i);
}
Entry Cache::promote(const QString &pack, const QString &snapshot, const QString &fingerprint,
                     const QString &contractDigest, const QJsonObject &validation,
                     const QString &mappingIdentity, const QString &metadataIdentity,
                     const QString &family, const QString &minecraftVersion) {
    if (!validation.value("valid").toBool() || !validation.value("injectionReady").toBool() ||
        validation.value("fingerprint").toString() != fingerprint)
        throw std::runtime_error("unvalidated candidate cannot be promoted");
    QDir().mkpath(m_root);
    QLockFile lock(m_root + "/cache.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        throw std::runtime_error("mapping cache is busy");
    auto i = index();
    auto verified = i.value("verified").toObject(), previous = i.value("previous").toObject();
    const auto cacheKey = mappingIdentity.isEmpty() ? fingerprint : mappingIdentity;
    const auto serial = quint64(i.value("promotionSerial").toDouble()) + 1;
    i["promotionSerial"] = double(serial);
    const auto revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto dir = m_root + "/objects/" + revision;
    if (!QDir().mkpath(dir) || !QFile::copy(pack, dir + "/pack.json") ||
        !QFile::copy(snapshot, dir + "/snapshot.json"))
        throw std::runtime_error("candidate staging failed; previous cache retained");
    const auto digest = fileDigest(dir + "/pack.json", 2 * 1024 * 1024);
    writeObject(dir + "/proof.json", {{"validated", true},
                                      {"promotionSerial", double(serial)},
                                      {"symbols", validation.value("symbols")},
                                      {"analyzerVersion", 5},
                                      {"mappingIdentity", mappingIdentity},
                                      {"metadataIdentity", metadataIdentity},
                                      {"bindingIdentity", validation.value("identity").toObject().value("bindingIdentity")},
                                      {"family", family},
                                      {"minecraftVersion", minecraftVersion},
                                      {"fingerprint", fingerprint},
                                      {"contractDigest", contractDigest},
                                      {"packDigest", digest},
                                      {"snapshotDigest", fileDigest(dir + "/snapshot.json")}});
    if (verified.contains(cacheKey))
        previous[cacheKey] = verified.value(cacheKey);
    verified[cacheKey] = revision;
    i["cacheVersion"] = 2;
    i["verified"] = verified;
    i["previous"] = previous;
    i["lastVerified"] = revision;
    i["lastFingerprint"] = cacheKey;
    i.remove("candidate");
    // Only this final atomic pointer update makes a staged object visible.
    writeObject(m_root + "/index.json", i);
    return {revision, fingerprint, dir + "/pack.json", dir + "/snapshot.json",
            digest, contractDigest, mappingIdentity, metadataIdentity, family, minecraftVersion, {}, serial,
            validation.value("identity").toObject().value("bindingIdentity").toString()};
}
bool Cache::rollback() {
    QDir().mkpath(m_root);
    QLockFile lock(m_root + "/cache.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return false;
    auto i = index();
    const auto key = i.value("lastFingerprint").toString();
    auto v = i.value("verified").toObject(), p = i.value("previous").toObject();
    if (key.isEmpty() || !p.contains(key))
        return false;
    auto old = p.value(key);
    if (QUuid(old.toString()).isNull() ||
        QUuid(old.toString()).toString(QUuid::WithoutBraces) != old.toString())
        return false;
    const auto proof = readObject(m_root + "/objects/" + old.toString() + "/proof.json");
    if (!entry(old.toString(), proof.value("contractDigest").toString()).valid())
        return false;
    p[key] = v.value(key);
    v[key] = old;
    i["verified"] = v;
    i["previous"] = p;
    i["lastVerified"] = old;
    writeObject(m_root + "/index.json", i);
    return true;
}
} // namespace mapping_cache
