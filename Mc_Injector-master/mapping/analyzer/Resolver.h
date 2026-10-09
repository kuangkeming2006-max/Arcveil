#pragma once
#include "Bytecode.h"
namespace mcoverlay::mapping {
Json selectDetailCandidates(const Json &pack, const Json &lite, const Json *reference,
                            const Json &contracts, const Events &events = {}, bool allowEmpty = false,
                            bool validationOnly = false, const Json *state = nullptr);
Json partitionDetailReuse(Json selection, const Json &lite, const Json &prior);
Json mergeDetailReuse(Json detail, const Json &selection, const Json &prior);
Json requiredClassNames(const Json &contracts, const Json &symbols);
Json mappingIdentity(const Json &pack, const Json &snapshot, const Json &contracts);
Json validateRuntime(const Json &pack, const Json &snapshot, const Json &contracts,
                     const Events &events = {});
Json resolveMappings(const Json &pack, const Json *reference, const Json &target,
                     const Json &contracts, const Events &events = {}, const Json *state = nullptr,
                     bool incremental = false);
} // namespace mcoverlay::mapping
