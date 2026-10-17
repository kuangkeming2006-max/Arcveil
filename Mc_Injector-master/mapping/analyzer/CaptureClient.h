#pragma once
#include "Analyzer.h"
#include <map>
namespace mcoverlay::mapping {
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
