#pragma once
#include "Bytecode.h"
namespace mcoverlay::mapping {
// Bundle dispatch is exact by gameVersion; a legacy contract is never applied to modern symbols.
const Json& contractsForPack(const Json& pack, const Json& contracts);
Json selectDetailCandidates(const Json &pack, const Json &lite, const Json *reference,
                            const Json &contracts, const Events &events = {}, bool allowEmpty = false,
                            bool validationOnly = false, const Json *state = nullptr);
Json partitionDetailReuse(Json selection, const Json &lite, const Json &prior);
Json mergeDetailReuse(Json detail, const Json &selection, const Json &prior);
Json requiredClassNames(const Json &contracts, const Json &symbols);
Json mappingIdentity(const Json &pack, const Json &snapshot, const Json &contracts, bool bindingOnly = false);
// Catalog entries carry {path, pack}; metadata candidates never authorize runtime identity.
Json identifyRuntime(const Json::Array &catalog, const Json &snapshot, const Json &contracts);
Json validateAuthoredRuntime(const Json &pack, const Json &snapshot, const Json &contracts,
                            const Events &events = {});
Json validateRuntime(const Json &pack, const Json &snapshot, const Json &contracts,
                     const Events &events = {});
Json validateCachedRuntime(const Json &pack, const Json &snapshot, const Json &contracts,
                           const std::string &bindingIdentity,
                           const Events &events = {});
Json resolveMappings(const Json &pack, const Json *reference, const Json &target,
                     const Json &contracts, const Events &events = {}, const Json *state = nullptr,
                     bool incremental = false);
} // namespace mcoverlay::mapping
