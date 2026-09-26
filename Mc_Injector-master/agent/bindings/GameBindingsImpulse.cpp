#include "GameBindingsCache.internal.h"
#include "SmartHotbarPolicy.h"
#include "TrajectoryMath.h"
#include "SafeWalkPolicy.h"

#include "src/AgentLog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcoverlay {

bool GameBindings::suppressKnownImpulse(JNIEnv* env,jobject entity,jobject attacker) noexcept
{
    const int selected=m_shieldAttacker.load(std::memory_order_acquire);
    const int local=m_shieldLocalPlayer.load(std::memory_order_acquire);
    const auto* c=m_cache.get();
    if(selected==-1||local<0||!env||!c||!entity||!c->getEntityId||!c->playerClass) return false;
    if(!attacker&&selected!=-2) return false;
    if(env->IsInstanceOf(entity,c->playerClass)!=JNI_TRUE||
       (selected!=-2&&env->IsInstanceOf(attacker,c->playerClass)!=JNI_TRUE)) return false;
    const int victim=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()) {clearException(env);return false;}
    if(selected==-2) return victim==local;
    const int source=env->CallIntMethod(attacker,c->getEntityId);
    if(env->ExceptionCheck()) {clearException(env);return false;}
    // This method receives the actual damage source, not an inferred attacker.
    // Unknown network velocity packets and unrelated motion never reach it.
    return victim==local&&source==selected&&source!=victim;
}


} // namespace mcoverlay
