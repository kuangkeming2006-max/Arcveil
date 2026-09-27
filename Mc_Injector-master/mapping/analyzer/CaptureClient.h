#pragma once
#include "Analyzer.h"
#include <map>
namespace mcoverlay::mapping {
Json captureLive(const std::map<QString, QString> &options, const Json &contracts,
                 const Events &events);
Json readSnapshotFile(const std::filesystem::path &path);
void writeSnapshotFile(const std::filesystem::path &path, const Json &snapshot);
} // namespace mcoverlay::mapping
