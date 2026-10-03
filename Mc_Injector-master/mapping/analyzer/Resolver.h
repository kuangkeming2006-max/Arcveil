#pragma once
#include "Bytecode.h"
namespace mcoverlay::mapping {
Json selectDetailCandidates(const Json &pack, const Json &lite, const Json *reference,
                            const Json &contracts, const Events &events = {}, bool allowEmpty = false);
Json validateRuntime(const Json &pack, const Json &snapshot, const Json &contracts,
                     const Events &events = {});
Json resolveMappings(const Json &pack, const Json *reference, const Json &target,
                     const Json &contracts, const Events &events = {}, const Json *state = nullptr,
                     bool incremental = false);
} // namespace mcoverlay::mapping
