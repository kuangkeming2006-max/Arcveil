#pragma once
#include "Bytecode.h"
namespace mcoverlay::mapping {
Json selectDetailCandidates(const Json &pack, const Json &lite, const Json *reference,
                            const Json &contracts, const Events &events = {});
Json validateRuntime(const Json &pack, const Json &snapshot, const Json &contracts,
                     const Events &events = {});
Json resolveMappings(const Json &pack, const Json *reference, const Json &target,
                     const Json &contracts, const Events &events = {});
} // namespace mcoverlay::mapping
