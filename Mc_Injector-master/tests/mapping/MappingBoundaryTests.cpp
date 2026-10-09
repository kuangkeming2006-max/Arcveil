#include "../../agent/bindings/MappingPack.h"
#include "../../agent/src/AgentOptions.h"
#include "../../mapping/cache/MappingCache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>
using namespace mapping_cache;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QTemporaryDir dir;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", message);
        }
    };
    const auto source = QString::fromLocal8Bit(argv[1]);
    const auto pack = dir.path() + "/pack.json", snapshot = dir.path() + "/snapshot.json";
    QFile::copy(source, pack);
    writeObject(snapshot, {{"test", true}});
    const auto digest = fileDigest(pack);
    auto loaded = mcoverlay::bindings::loadMappingPack(std::filesystem::path(pack.toStdWString()),
                                                       digest.toStdString());
    check(loaded.schemaVersion == 1, "digest-checked pack loads");
    try {
        (void)mcoverlay::bindings::loadMappingPack(std::filesystem::path(pack.toStdWString()),
                                                   std::string(64, '0'));
        check(false, "bad digest rejected");
    } catch (...) {
        check(true, "bad digest rejected");
    }
    mcoverlay::bindings::MappingRegistry registry(std::filesystem::path(pack.toStdWString()),
                                                  digest.toStdString());
    check(registry.freeze(), "verified registry freezes");
    check(registry.registerDictionary(loaded.providers[0].dictionaries[0]) ==
              mcoverlay::bindings::MappingRegistrationResult::Frozen,
          "late registration denied");
    const std::string base =
        "pipe=\\\\.\\pipe\\mapping-test;token=0123456789abcdef0123456789abcdef;protocol=1";
    const auto options = base + ";mapping=" + pack.toUtf8().toHex().toStdString() +
                         ";mappingHash=" + digest.toStdString();
    check(mcoverlay::parseAgentOptions(base.c_str()).valid(), "legacy options unchanged");
    check(mcoverlay::parseAgentOptions(options.c_str()).valid(), "verified options valid");
    for (const auto &suffix :
         {std::string(";mapping=00"), std::string(";mapping=abc"),
          std::string(";mappingHash=") + digest.toStdString(), std::string(";mapping=GG")})
        check(!mcoverlay::parseAgentOptions((base + suffix).c_str()).valid(),
              "malformed partial mapping options fail closed");
    check(!mcoverlay::parseAgentOptions((options + ";mapping=00").c_str()).valid(),
          "duplicate mapping option rejected");
    const auto binding = options + ";bindingRequired=1;mappingAnchor=4c613b;mappingLoaderType=4c623b;mappingLoaderInstance=123";
    check(mcoverlay::parseAgentOptions(binding.c_str()).valid(), "complete exact-loader binding options accepted");
    for (const auto &suffix : {std::string(";bindingRequired=1"), std::string(";mappingAnchor=4c613b"),
         std::string(";bindingRequired=1;mappingAnchor=GG;mappingLoaderType=4c623b;mappingLoaderInstance=123")})
        check(!mcoverlay::parseAgentOptions((options + suffix).c_str()).valid(), "partial or malformed loader pin fails closed");
    check(!mcoverlay::parseAgentOptions((binding + ";mappingLoaderInstance=123").c_str()).valid(), "duplicate loader pin rejected");
    Cache cache(dir.path() + "/cache");
    cache.candidate("run-one", "fingerprint", "analyzing");
    check(!cache.lookup("fingerprint", "contract").valid(), "candidate cannot be cache hit");
    QJsonObject proof{{"valid", true}, {"injectionReady", true}, {"fingerprint", "fingerprint"}};
    auto a = cache.promote(pack, snapshot, "fingerprint", "contract", proof);
    check(cache.lookup("fingerprint", "contract").revision == a.revision, "verified promoted");
    check(!cache.lookup("fingerprint", "new-contract").valid(),
          "contract changes invalidate cache");
    auto b = cache.promote(pack, snapshot, "fingerprint", "contract", proof);
    check(b.revision != a.revision && cache.rollback() &&
              cache.lookup("fingerprint", "contract").revision == a.revision,
          "previous revision rollback");
    auto invalid = proof;
    invalid["valid"] = false;
    try {
        cache.promote(pack, snapshot, "fingerprint", "contract", invalid);
        check(false, "invalid promotion");
    } catch (...) {
        check(cache.lookup("fingerprint", "contract").revision == a.revision,
              "failed promotion retains verified");
    }
    Cache policy(dir.path() + "/policy");
    const auto same = policy.promote(pack, snapshot, "fingerprint", "contract", proof,
        "lunar-8", "metadata-8", "Lunar", "1.8.9");
    const auto distant = policy.promote(pack, snapshot, "fingerprint", "contract", proof,
        "lunar-20", "metadata-20", "Lunar", "1.20.1");
    policy.promote(pack, snapshot, "fingerprint", "contract", proof,
        "badlion", "metadata-b", "Badlion", "1.8.9");
    check(policy.reference("contract", "Lunar", "1.20.1", "lunar-8").revision == same.revision,
          "exact stable identity outranks version and unrelated lastVerified");
    check(policy.reference("contract", "Lunar", "1.7.10").revision == same.revision,
          "same-family nearest Minecraft reference outranks newest distant version");
    check(!policy.reference("contract", "Forge", "1.8.9").valid(), "no cross-family lastVerified reference");
    QFile history(same.snapshot); history.open(QIODevice::WriteOnly | QIODevice::Append);
    history.write("corrupt historical snapshot"); history.close();
    check(policy.candidates("Lunar", "1.8.9", "contract").size() == 1,
          "live-validation candidate discovery does not read historical snapshots");
    check(policy.reference("contract", "Lunar", "1.8.9").revision == distant.revision,
          "structural reference checks source integrity and falls through corruption");
    QLockFile lock(dir.path() + "/cache/cache.lock");
    lock.lock();
    try {
        cache.promote(pack, snapshot, "fingerprint", "contract", proof);
        check(false, "concurrent promotion blocked");
    } catch (...) {
        check(true, "concurrent promotion blocked");
    }
    lock.unlock();
    QFile corrupt(a.pack);
    corrupt.open(QIODevice::WriteOnly | QIODevice::Append);
    corrupt.write("tampered");
    corrupt.close();
    check(!cache.lookup("fingerprint", "contract").valid(), "corrupt verified cache rejected");
    const auto indexPath = dir.path() + "/cache/index.json";
    auto index = readObject(indexPath);
    index["previous"] = QJsonObject{{"fingerprint", "../../outside"}};
    writeObject(indexPath, index);
    check(!cache.rollback(), "malformed previous revision rejected before path access");
    QFile large(dir.path() + "/large-stream.jsonl");
    check(large.open(QIODevice::WriteOnly), "large snapshot fixture opens");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk(1024 * 1024, 'x');
    for (int i = 0; i < 65; ++i) {
        large.write(chunk);
        hash.addData(chunk);
    }
    large.close();
    check(fileDigest(large.fileName()) == QString::fromLatin1(hash.result().toHex()),
          "stream digest has no aggregate 64 MiB limit");
    try {
        fileDigest(large.fileName(), 2 * 1024 * 1024);
        check(false, "explicit pack-sized digest bound");
    } catch (...) {
        check(true, "explicit pack-sized digest bound");
    }
    std::printf("Mapping boundaries/cache: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
