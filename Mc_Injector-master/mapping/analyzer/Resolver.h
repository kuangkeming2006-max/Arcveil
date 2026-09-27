#pragma once
#include "Bytecode.h"
namespace mcoverlay::mapping {
Json validateRuntime(const Json& pack,const Json& snapshot,const Json& contracts,const Events& events={});
Json resolveMappings(const Json& pack,const Json* reference,const Json& target,const Json& contracts,const Events& events={});
}
