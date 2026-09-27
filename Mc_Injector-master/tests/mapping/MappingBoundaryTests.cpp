#include "../../agent/bindings/MappingPack.h"
#include "../../agent/src/AgentOptions.h"
#include "../../mapping/cache/MappingCache.h"
#include <QCoreApplication>
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
    const auto indexPath=dir.path()+"/cache/index.json";
    auto index=readObject(indexPath);
    index["previous"]=QJsonObject{{"fingerprint","../../outside"}};
    writeObject(indexPath,index);
    check(!cache.rollback(), "malformed previous revision rejected before path access");
    std::printf("Mapping boundaries/cache: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
