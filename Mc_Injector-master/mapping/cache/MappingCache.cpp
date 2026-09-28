#include "MappingCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>
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
    if (value.value("cacheVersion").toInt() != 1)
        throw std::runtime_error("unsupported mapping cache version");
    return value;
}
Entry Cache::entry(const QString &revision, const QString &contractDigest) const {
    if (!QRegularExpression("^[a-f0-9-]{36}$").match(revision).hasMatch())
        return {};
    try {
        const auto dir = m_root + "/objects/" + revision;
        const auto proof = readObject(dir + "/proof.json");
        if (!proof.value("validated").toBool() ||
            proof.value("contractDigest").toString() != contractDigest ||
            proof.value("analyzerVersion").toInt() != 4)
            return {};
        if (fileDigest(dir + "/pack.json", 2 * 1024 * 1024) !=
                proof.value("packDigest").toString() ||
            fileDigest(dir + "/snapshot.json") != proof.value("snapshotDigest").toString())
            return {};
        return {revision,
                proof.value("fingerprint").toString(),
                dir + "/pack.json",
                dir + "/snapshot.json",
                proof.value("packDigest").toString(),
                contractDigest};
    } catch (...) {
        return {};
    }
}
Entry Cache::lookup(const QString &fingerprint, const QString &contractDigest) const {
    auto e =
        entry(index().value("verified").toObject().value(fingerprint).toString(), contractDigest);
    return e.fingerprint == fingerprint ? e : Entry{};
}
Entry Cache::reference(const QString &contractDigest) const {
    return entry(index().value("lastVerified").toString(), contractDigest);
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
                     const QString &contractDigest, const QJsonObject &validation) {
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
    const auto revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto dir = m_root + "/objects/" + revision;
    if (!QDir().mkpath(dir) || !QFile::copy(pack, dir + "/pack.json") ||
        !QFile::copy(snapshot, dir + "/snapshot.json"))
        throw std::runtime_error("candidate staging failed; previous cache retained");
    const auto digest = fileDigest(dir + "/pack.json", 2 * 1024 * 1024);
    writeObject(dir + "/proof.json", {{"validated", true},
                                      {"analyzerVersion", 4},
                                      {"fingerprint", fingerprint},
                                      {"contractDigest", contractDigest},
                                      {"packDigest", digest},
                                      {"snapshotDigest", fileDigest(dir + "/snapshot.json")}});
    if (verified.contains(fingerprint))
        previous[fingerprint] = verified.value(fingerprint);
    verified[fingerprint] = revision;
    i["verified"] = verified;
    i["previous"] = previous;
    i["lastVerified"] = revision;
    i["lastFingerprint"] = fingerprint;
    i.remove("candidate");
    // Only this final atomic pointer update makes a staged object visible.
    writeObject(m_root + "/index.json", i);
    return {revision, fingerprint,   dir + "/pack.json", dir + "/snapshot.json",
            digest,   contractDigest};
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
