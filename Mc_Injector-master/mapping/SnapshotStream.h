#pragma once
#include "Json.h"
#include <functional>
namespace mcoverlay::mapping {
// Limits apply to a single frame/object, never to the aggregate snapshot file.
inline constexpr std::size_t snapshotChunkBytes = 64U * 1024U;
inline constexpr std::size_t snapshotFrameBytes = 512U * 1024U;
inline constexpr std::size_t snapshotObjectBytes = 48U * 1024U * 1024U;
struct SnapshotLimit : std::runtime_error {
    std::string layer, name;
    std::size_t actual, limit;
    SnapshotLimit(std::string layer_, std::string name_, std::size_t actual_, std::size_t limit_)
        : std::runtime_error("snapshot size limit: layer=" + layer_ + " limit=" + name_ +
                             " observed=" + std::to_string(actual_) +
                             " configured=" + std::to_string(limit_)),
          layer(std::move(layer_)), name(std::move(name_)), actual(actual_), limit(limit_) {}
    Json json() const {
        return Json::Object{{"reason", what()},
                            {"limitLayer", layer},
                            {"limitName", name},
                            {"observedBytes", double(actual)},
                            {"configuredLimit", double(limit)}};
    }
};
inline void snapshotFrames(const char *kind, std::size_t index, const Json &value,
                           const std::function<void(const std::string &)> &writeFrame) {
    const auto text = value.dump();
    if (text.size() > snapshotObjectBytes)
        throw SnapshotLimit("buffer", std::string("snapshot-object:") + kind, text.size(),
                            snapshotObjectBytes);
    std::size_t offset = 0, part = 0;
    while (offset < text.size()) {
        auto end = std::min(offset + snapshotChunkBytes, text.size());
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            --end;
        auto line = Json(Json::Object{{"streamVersion", 1},
                                      {"kind", kind},
                                      {"index", double(index)},
                                      {"part", double(part++)},
                                      {"last", end == text.size()},
                                      {"data", text.substr(offset, end - offset)}})
                        .dump() +
                    "\n";
        if (line.size() > snapshotFrameBytes)
            throw SnapshotLimit("protocol", "snapshot-jsonl-frame", line.size(),
                                snapshotFrameBytes);
        writeFrame(line);
        offset = end;
    }
}
class SnapshotWriter {
    std::ofstream file;

  public:
    std::size_t bytes = 0;
    explicit SnapshotWriter(const std::filesystem::path &path)
        : file(path, std::ios::binary | std::ios::trunc) {
        if (!file)
            throw std::runtime_error("snapshot stream file open failed");
        const std::string magic = "{\"snapshotStreamVersion\":1}\n";
        file.write(magic.data(), std::streamsize(magic.size()));
        bytes = magic.size();
    }
    void object(const char *kind, std::size_t index, const Json &value) {
        snapshotFrames(kind, index, value, [&](const std::string &line) {
            file.write(line.data(), std::streamsize(line.size()));
            bytes += line.size();
            if (!file)
                throw std::runtime_error(
                    "snapshot stream file write failed (disk/OS error; no aggregate file limit)");
        });
    }
    void finish() {
        file.flush();
        if (!file)
            throw std::runtime_error("snapshot stream file flush failed");
        file.close();
    }
};
inline Json readSnapshotStream(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open snapshot stream");
    char magicBuffer[64]{};
    input.getline(magicBuffer, 64);
    std::string magic(magicBuffer);
    if (input.fail() || magic != "{\"snapshotStreamVersion\":1}")
        throw std::runtime_error("unsupported snapshot stream header");
    Json snapshot;
    Json::Array classes;
    std::string payload, kind;
    std::size_t part = 0, index = 0;
    bool header = false, complete = false;
    std::vector<char> buffer(snapshotFrameBytes + 2);
    while (input.peek() != std::char_traits<char>::eof()) {
        input.getline(buffer.data(), std::streamsize(buffer.size()));
        if (input.fail())
            throw SnapshotLimit("protocol", "snapshot-jsonl-frame", snapshotFrameBytes + 1,
                                snapshotFrameBytes);
        const std::string_view line(buffer.data(), std::size_t(input.gcount() - 1));
        Json frame;
        try {
            frame = Json::parse(line);
        } catch (const std::exception &e) {
            throw std::runtime_error(std::string("snapshot frame parse: ") + e.what());
        }
        if (complete || frame.at("streamVersion").integer() != 1 ||
            frame.at("part").number() != double(part))
            throw std::runtime_error("snapshot stream version/order mismatch");
        if (part == 0) {
            kind = frame.at("kind").string();
            index = std::size_t(frame.at("index").number());
        }
        if (frame.at("kind").string() != kind || frame.at("index").number() != double(index))
            throw std::runtime_error("snapshot chunk identity mismatch");
        const auto &chunk = frame.at("data").string();
        if (chunk.size() > snapshotChunkBytes)
            throw SnapshotLimit("protocol", "snapshot-chunk-data", chunk.size(),
                                snapshotChunkBytes);
        if (payload.size() + chunk.size() > snapshotObjectBytes)
            throw SnapshotLimit("buffer", "snapshot-object:" + kind, payload.size() + chunk.size(),
                                snapshotObjectBytes);
        payload += chunk;
        ++part;
        if (!frame.at("last").boolean())
            continue;
        Json value;
        try {
            value = Json::parse(payload);
        } catch (const std::exception &e) {
            throw std::runtime_error("snapshot object parse (" + kind + "): " + e.what());
        }
        if (!header && kind == "header" && index == 0) {
            snapshot = std::move(value);
            header = true;
        } else if (header && kind == "class" && index == classes.size()) {
            if (classes.size() >= 100000)
                throw SnapshotLimit("protocol", "snapshot-class-count", classes.size() + 1, 100000);
            classes.push_back(std::move(value));
        } else if (header && kind == "footer" && index == classes.size()) {
            for (const auto &[key, item] : value.object())
                snapshot[key] = item;
            complete = true;
        } else
            throw std::runtime_error("snapshot object order mismatch");
        payload.clear();
        part = 0;
    }
    if (!complete || part || !snapshot.at("complete").boolean())
        throw std::runtime_error("incomplete snapshot stream");
    snapshot["classes"] = std::move(classes);
    return snapshot;
}
} // namespace mcoverlay::mapping
