#include "Analyzer.h"
#include "../../agent/bindings/VersionAdapter.h"
#include "../ProbeProtocol.h"
#include "../../agent/bindings/MappingPack.h"
#include <QCryptographicHash>
#include <QSaveFile>
#include <algorithm>
#include <set>

namespace mcoverlay::mapping {
std::filesystem::path filePath(const QString &path) {
    return std::filesystem::path(path.toStdWString());
}
QString qtPath(const std::filesystem::path &path) {
    return QString::fromStdWString(path.wstring());
}
void writeJson(const std::filesystem::path &path, const Json &value) {
    QSaveFile file(qtPath(path));
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("cannot open output");
    const auto bytes = value.dump();
    if (file.write(bytes.data(), qint64(bytes.size())) != qint64(bytes.size()) || !file.commit())
        throw std::runtime_error("atomic output failed");
}
std::string sha256(std::string_view value) {
    return QCryptographicHash::hash(QByteArrayView(value.data(), qsizetype(value.size())),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}
// Decorate once: comparing serialized classes repeatedly multiplies capture cost.
static void canonicalSort(Json::Array &items) {
    std::vector<std::pair<std::string, Json>> sorted;
    sorted.reserve(items.size());
    for (auto &item : items) { auto key = item.dump(); sorted.emplace_back(std::move(key), std::move(item)); }
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    items.clear();
    for (auto &item : sorted) items.push_back(std::move(item.second));
}
Json classMetadata(Json klass) {
    for (const auto *key :
         {"constantPool", "constantPoolCount", "major", "minor", "crossReferences", "installedDigest", "installedProofKind"})
        klass.object().erase(key);
    for (auto &m : klass["methods"].array())
        m.object().erase("bytecode");
    for (const auto *kind : {"fields", "methods", "interfaces"}) canonicalSort(klass[kind].array());
    return klass;
}
static std::string snapshotDigest(const Json &snapshot) {
    Json content = Json::Object{{"snapshotVersion", 1},
                                {"classes", snapshot.at("classes")},
                                {"launchEvidence", Json::Array{}}};
    // Runtime drift checks bind only the captured classes and live loader continuity.
    // The request's lite fingerprint includes unrelated loaded classes.
    auto runtimeLoader = [&](const Json &key) -> Json {
        if (snapshot.contains("loaderInstances"))
            for (const auto &item : snapshot.at("loaderInstances").array())
                if (item.at("loaderKey") == key)
                    return item.at("type").string() + ":" + item.at("instance").dump();
        return key;
    };
    for (auto &klass : content["classes"].array()) {
        klass["loaderKey"] = runtimeLoader(klass.at("loaderKey"));
        klass["super"]["loaderKey"] = runtimeLoader(klass.at("super").at("loaderKey"));
        for (auto &item : klass["interfaces"].array()) item["loaderKey"] = runtimeLoader(item.at("loaderKey"));
        for (const auto *kind : {"fields", "methods", "interfaces"}) canonicalSort(klass[kind].array());
    }
    canonicalSort(content["classes"].array());
    canonicalSort(content["launchEvidence"].array());
    return sha256(content.dump());
}
Json refreshSnapshotFingerprint(Json snapshot) {
    const bool lite = snapshot.contains("detailLevel") && snapshot.at("detailLevel").string() == "lite";
    const auto digest = snapshotDigest(snapshot);
    snapshot["fingerprint"] = lite ? sha256("lite-v1:" + digest) : digest;
    std::size_t methods = 0, fields = 0;
    for (const auto &c : snapshot.at("classes").array()) {
        methods += c.at("methods").array().size(); fields += c.at("fields").array().size();
    }
    snapshot["methodCount"] = double(methods); snapshot["fieldCount"] = double(fields);
    return inspectSnapshot(std::move(snapshot));
}
Json inspectSnapshot(Json snapshot) {
    const bool lite =
        snapshot.contains("detailLevel") && snapshot.at("detailLevel").string() == "lite";
    const bool detail =
        snapshot.contains("detailLevel") && snapshot.at("detailLevel").string() == "selected";
    if (snapshot.at("snapshotVersion").integer() != 1 || !snapshot.at("complete").boolean() ||
        snapshot.at("captureKind").string() != (lite     ? "jvmti-metadata-double-read"
                                                : detail ? "jvmti-selected-detail-double-read"
                                                         : "jvmti-installed-double-read"))
        throw std::runtime_error("incomplete/unsupported live snapshot");
    if (snapshot.contains("normalizedVersion")) {
        if (snapshot.at("normalizedVersion").integer() != 1 && snapshot.at("normalizedVersion").integer() != 2)
            throw std::runtime_error("unsupported normalized snapshot");
        if (snapshot.at("normalizedVersion").integer() == 1) {
            Json legacy = Json::Object{{"snapshotVersion", 1}, {"classes", snapshot.at("classes")},
                {"launchEvidence", snapshot.contains("launchEvidence") ? snapshot.at("launchEvidence") : Json(Json::Array{})}};
            if (snapshot.contains("captureScope")) legacy["captureScope"] = snapshot.at("captureScope");
            const auto oldDigest = sha256(legacy.dump());
            if (snapshot.at("fingerprint").string() != (lite ? sha256("lite-v1:" + oldDigest) : oldDigest))
                throw std::runtime_error("legacy snapshot fingerprint mismatch");
            snapshot["normalizedVersion"] = 2;
            return refreshSnapshotFingerprint(std::move(snapshot));
        }
        const auto digest = snapshotDigest(snapshot);
        if (snapshot.at("fingerprint").string() != (lite ? sha256("lite-v1:" + digest) : digest))
            throw std::runtime_error("snapshot fingerprint mismatch");
        if (snapshot.at("classes").array().empty())
            throw std::runtime_error("empty snapshot");
        return snapshot;
    }
    auto &classes = snapshot["classes"].array();
    if (classes.empty() || classes.size() > 100000)
        throw std::runtime_error("empty/oversize snapshot");
    std::map<int, std::string> loaderKeys;
    std::set<std::string> groups;
    std::map<int, std::string> contentKeys;
    std::map<std::string, int> counts;
    for (const auto &loader : snapshot.at("loaders").array()) {
        const int id = loader.at("id").integer();
        Json::Array names;
        for (const auto &c : classes)
            if (c.at("loader").integer() == id)
                names.push_back(c.at("name"));
        std::sort(names.begin(), names.end(),
                  [](const Json &a, const Json &b) { return a.string() < b.string(); });
        auto key =
            sha256(Json(Json::Object{{"type", loader.at("type")}, {"classes", names}}).dump());
        contentKeys[id] = key;
        ++counts[key];
    }
    for (const auto &loader : snapshot.at("loaders").array()) {
        const int id = loader.at("id").integer();
        auto key = contentKeys.at(id);
        if (counts.at(key) > 1) {
            if (!loader.contains("instance"))
                throw std::runtime_error(
                    "indistinguishable defining loaders; runtime instance identity required");
            key = sha256("process-loader:" + key + ":" + loader.at("instance").dump());
            snapshot["processScopedLoaders"] = true;
        }
        if (!groups.insert(key).second)
            throw std::runtime_error(
                "classloader identity hash collision; refusing to merge scopes");
        loaderKeys[id] = key;
    }
    if (detail) {
        std::map<std::string, Json> bindings;
        for (const auto &binding : snapshot.at("loaderBindings").array()) {
            const auto id = binding.at("type").string() + ":" + binding.at("instance").dump();
            if (!bindings.emplace(id, binding).second)
                throw std::runtime_error("ambiguous requested loader identity");
        }
        for (const auto &loader : snapshot.at("loaders").array()) {
            auto it =
                bindings.find(loader.at("type").string() + ":" + loader.at("instance").dump());
            if (it == bindings.end())
                throw RuntimeChanged("classloader set changed since lite capture");
            loaderKeys[loader.at("id").integer()] = it->second.at("loaderKey").string();
        }
    }
    Json::Array instances;
    for (const auto &loader : snapshot.at("loaders").array())
        if (loader.contains("instance"))
            instances.push_back(
                Json::Object{{"type", loader.at("type")},
                             {"instance", loader.at("instance")},
                             {"loaderKey", loaderKeys.at(loader.at("id").integer())}});
    snapshot["loaderInstances"] = instances;
    loaderKeys[0] = "bootstrap";
    const auto convert = [&](Json &ref) {
        const int id = ref.at("loader").integer();
        if (!loaderKeys.contains(id))
            throw std::runtime_error("unknown defining loader");
        ref["loaderKey"] = loaderKeys.at(id);
        ref.object().erase("loader");
    };
    std::set<std::string> ids;
    std::size_t methods = 0, fields = 0;
    for (auto &c : classes) {
        convert(c);
        convert(c["super"]);
        for (auto &ref : c["interfaces"].array())
            convert(ref);
        const auto id = c.at("loaderKey").string() + c.at("name").string();
        if (!ids.insert(id).second)
            throw std::runtime_error("duplicate class identity");
        methods += c.at("methods").array().size();
        fields += c.at("fields").array().size();
        if (!lite) {
            (void)c.at("constantPoolCount").integer();
            (void)c.at("constantPool").string();
        } else if (c.contains("constantPool"))
            throw std::runtime_error("lite snapshot contains detailed data");
    }
    std::sort(classes.begin(), classes.end(), [](const Json &a, const Json &b) {
        return a.at("loaderKey").string() + a.at("name").string() <
               b.at("loaderKey").string() + b.at("name").string();
    });
    // Runtime identity is tracked separately: the content fingerprint can be reused
    // across sessions, but no snapshot from another process authorizes injection.
    const auto fingerprint = snapshotDigest(snapshot);
    snapshot["fingerprint"] = lite ? sha256("lite-v1:" + fingerprint) : fingerprint;
    snapshot["methodCount"] = double(methods);
    snapshot["fieldCount"] = double(fields);
    snapshot["normalizedVersion"] = 2;
    snapshot.object().erase("loaders");
    return snapshot;
}
Json validatePack(const Json &pack) {
    const auto parsed = bindings::parseMappingPack(pack);
    int count = 0;
    for (const auto &p : parsed.providers)
        count += int(p.dictionaries.size());
    return Json::Object{{"valid", true},
                        {"level", "schema"},
                        {"injectionReady", false},
                        {"apiSupported", bindings::selectVersionAdapter(bindings::MinecraftVersion::parse(parsed.gameVersion)) != nullptr},
                        {"versionSource", "pack-declared; requires live API validation"},
                        {"packId", parsed.id},
                        {"dictionaries", count}};
}
Json diffPacks(const Json &before, const Json &after) {
    (void)bindings::parseMappingPack(before);
    (void)bindings::parseMappingPack(after);
    std::map<std::string, Json> a, b;
    for (const auto &p : before.at("providers").array())
        for (const auto &d : p.at("dictionaries").array())
            a.emplace(d.at("id").string(), d);
    for (const auto &p : after.at("providers").array())
        for (const auto &d : p.at("dictionaries").array())
            b.emplace(d.at("id").string(), d);
    Json::Array changes;
    std::set<std::string> ids;
    for (const auto &[id, d] : a)
        ids.insert(id);
    for (const auto &[id, d] : b)
        ids.insert(id);
    for (const auto &id : ids) {
        if (!a.contains(id) || !b.contains(id)) {
            changes.push_back(
                Json::Object{{"dictionary", id}, {"kind", a.contains(id) ? "removed" : "added"}});
            continue;
        }
        for (const auto &[key, value] : a.at(id).at("symbols").object())
            if (value != b.at(id).at("symbols").at(key))
                changes.push_back(Json::Object{{"dictionary", id},
                                               {"symbol", key},
                                               {"before", value},
                                               {"after", b.at(id).at("symbols").at(key)}});
        for (const auto *key : {"label", "family", "detection", "mappingNamespace"}) {
            const auto oldValue=a.at(id).contains(key)?a.at(id).at(key):Json();
            const auto newValue=b.at(id).contains(key)?b.at(id).at(key):Json();
            if (oldValue != newValue)
                changes.push_back(Json::Object{{"dictionary", id},
                                               {"metadata", key},
                                               {"before", oldValue},
                                               {"after", newValue}});
        }
    }
    return Json::Object{{"changed", before != after},
                        {"changes", changes},
                        {"beforeDigest", sha256(before.dump())},
                        {"afterDigest", sha256(after.dump())}};
}
} // namespace mcoverlay::mapping
