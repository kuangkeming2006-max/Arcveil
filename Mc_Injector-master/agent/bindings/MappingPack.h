#pragma once
#include "MappingProvider.h"
#include "../../mapping/Json.h"

namespace mcoverlay::bindings {
struct MappingPackProvider {
    std::string id;
    ClientFamily family=ClientFamily::Unknown;
    int priority=0;
    std::vector<DetectionPattern> detection;
    std::vector<MappingDictionary> dictionaries;
};
struct MappingPack {
    int schemaVersion=1,packVersion=1;
    std::string id,gameVersion,source;
    std::vector<MappingPackProvider> providers;
};
// Parsing completes and validates the entire pack before registration begins.
[[nodiscard]] MappingPack parseMappingPack(const mapping::Json& document);
[[nodiscard]] MappingPack loadMappingPack(const std::filesystem::path& path);
[[nodiscard]] mapping::Json mappingDictionaryJson(const MappingDictionary& dictionary);
[[nodiscard]] mapping::Json mappingPackJson(const MappingPack& pack);
[[nodiscard]] std::filesystem::path defaultMappingPackPath();
}
