#pragma once
#include "Analyzer.h"
#include <map>
namespace mcoverlay::mapping {
struct CaptureScopeUnavailable : std::runtime_error {
    unsigned pid;
    std::string processStart;
    CaptureScopeUnavailable(unsigned value,std::string start)
        :std::runtime_error("selected authored API classes are absent from the pinned defining loader"),
         pid(value),processStart(std::move(start)) {}
};
// Pure request planning: historical identity catalogs never enlarge a capture.
// The caller adds the per-attempt output path before enforcing the transport cap.
Json planCaptureRequest(const std::map<QString, QString> &options, const Json &contracts,
                        const std::string &requestId = "plan");
Json captureRequestStats(const Json &request);
Json captureLive(const std::map<QString, QString> &options, const Json &contracts,
                 const Events &events);
Json readSnapshotFile(const std::filesystem::path &path);
void writeSnapshotFile(const std::filesystem::path &path, const Json &snapshot);
} // namespace mcoverlay::mapping
