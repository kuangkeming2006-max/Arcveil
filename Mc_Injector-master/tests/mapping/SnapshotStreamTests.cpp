#include <QCoreApplication>
#include "../../mapping/SnapshotStream.h"
#include <QTemporaryDir>
#include <cstdio>
using namespace mcoverlay::mapping;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const auto root = std::filesystem::path(dir.path().toStdWString());
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", message);
        }
    };
    auto path = root / L"large.jsonl";
    std::size_t written = 0;
    {
        SnapshotWriter stream(path);
        stream.object("header", 0, Json::Object{{"snapshotVersion", 1}});
        std::string payload(128 * 1024, 'x');
        payload.replace(65534, 2, "你好");
        for (int i = 0; i < 520; ++i)
            stream.object("class", i, Json::Object{{"name", std::to_string(i)}, {"data", payload}});
        stream.object("footer", 520, Json::Object{{"complete", true}});
        stream.finish();
        written = stream.bytes;
    }
    auto large = readSnapshotStream(path);
    check(written > 64U * 1024U * 1024U && large.at("classes").array().size() == 520,
          "aggregate snapshot exceeds old 48/64 MiB limits without increasing a buffer");
    check(large.at("classes").array()[0].at("data").string().find("你好") != std::string::npos,
          "Unicode split boundary roundtrip");
    check(std::filesystem::file_size(path) == written, "wire byte accounting exact");
    large = Json();
    auto bad = root / L"bad.jsonl";
    {
        std::ofstream file(bad, std::ios::binary);
        file << "{\"snapshotStreamVersion\":1}\n";
        snapshotFrames("header", 0, Json::Object{}, [&](const auto &line) { file << line; });
    }
    try {
        readSnapshotStream(bad);
        check(false, "truncated rejected");
    } catch (const std::exception &e) {
        check(std::string(e.what()).find("incomplete") != std::string::npos, "truncated rejected");
    }
    {
        std::ofstream file(bad, std::ios::binary);
        file << "{\"snapshotStreamVersion\":1}\n"
             << std::string(snapshotFrameBytes + 2, 'x') << '\n';
    }
    try {
        readSnapshotStream(bad);
        check(false, "frame cap");
    } catch (const SnapshotLimit &e) {
        check(e.layer == "protocol" && e.name == "snapshot-jsonl-frame" &&
                  e.limit == snapshotFrameBytes,
              "frame limit reports layer/name/configured value");
    }
    try {
        snapshotFrames("class", 0, Json::Object{{"data", std::string(snapshotObjectBytes, 'x')}},
                       [](const auto &) {});
        check(false, "object cap");
    } catch (const SnapshotLimit &e) {
        check(e.layer == "buffer" && e.name == "snapshot-object:class",
              "object limit distinguished from file/protocol limit");
    }
    {
        std::ofstream file(bad, std::ios::binary);
        file << "{\"snapshotStreamVersion\":1}\n";
        snapshotFrames("class", 0, Json::Object{}, [&](const auto &line) { file << line; });
    }
    try {
        readSnapshotStream(bad);
        check(false, "order");
    } catch (const std::exception &e) {
        check(std::string(e.what()).find("order") != std::string::npos,
              "out of order object rejected");
    }
    std::printf("Snapshot stream: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
