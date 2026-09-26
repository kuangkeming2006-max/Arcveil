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

GameSnapshot GameBindings::snapshot(const std::uint64_t tickMilliseconds) const noexcept
{
    // m_snapshot has one owner: the SwapBuffers render thread. Returning a copy
    // also keeps OverlayRenderer from retaining memory that release() resets.
    GameSnapshot result = m_snapshot;
    const ResolutionPhase phase = m_resolutionPhase.load(std::memory_order_acquire);
    result.mappingAttempt = m_mappingAttempt.load(std::memory_order_acquire);

    const std::uint64_t retryAt = m_retryAtMilliseconds.load(std::memory_order_acquire);
    const std::uint64_t retryRemaining = retryAt > tickMilliseconds
        ? retryAt - tickMilliseconds : 0U;
    result.mappingRetryInMs = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(retryRemaining, UINT32_MAX));

    switch (phase) {
    case ResolutionPhase::Resolved:
        // m_cache and every referenced ID were written before the resolver's
        // release-store of Resolved, so this acquire read makes them visible.
        if (m_cache != nullptr && m_cache->profile != nullptr) {
            result.mapping = m_cache->profile->label.c_str();
        }
        if (result.state == GameSnapshot::State::Resolving ||
            result.state == GameSnapshot::State::Unsupported) {
            result.state = GameSnapshot::State::WaitingForGameThread;
        }
        break;
    case ResolutionPhase::Unsupported:
        result.state = GameSnapshot::State::Unsupported;
        result.mapping = "unsupported client mappings";
        result.mappingRetryInMs = 0U;
        break;
    case ResolutionPhase::Unavailable:
        result.state = GameSnapshot::State::JniError;
        result.mapping = "mapping resolver unavailable";
        result.mappingRetryInMs = 0U;
        break;
    case ResolutionPhase::Stopped:
        result.state = GameSnapshot::State::Unsupported;
        result.mapping = "mapping resolver stopped";
        result.mappingRetryInMs = 0U;
        break;
    case ResolutionPhase::Resolving:
        result.state = GameSnapshot::State::Resolving;
        result.mapping = result.mappingAttempt == 0U
            ? "mapping resolver queued"
            : "probing mapping providers";
        break;
    }
    return result;
}

void GameBindings::sampleCamera(JNIEnv* const env) noexcept
{
    m_snapshot.camera.valid = false;
    if (env == nullptr ||
        m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved ||
        m_cache == nullptr) {
        return;
    }
    BindingCache* const cache = m_cache.get();
    if (cache->renderManagerObject == nullptr || cache->timerObject == nullptr ||
        cache->modelViewBuffer == nullptr ||
        cache->projectionBuffer == nullptr || cache->viewportBuffer == nullptr) {
        return;
    }

    WorldCameraSnapshot camera{};
    camera.renderX = env->GetDoubleField(cache->renderManagerObject,
                                         cache->renderPosition[0U]);
    camera.renderY = env->GetDoubleField(cache->renderManagerObject,
                                         cache->renderPosition[1U]);
    camera.renderZ = env->GetDoubleField(cache->renderManagerObject,
                                         cache->renderPosition[2U]);
    camera.partialTicks = env->GetFloatField(cache->timerObject,
                                             cache->renderPartialTicks);
    if (env->ExceptionCheck() == JNI_TRUE) {
        clearException(env);
        return;
    }

    const auto* const modelView = static_cast<const float*>(
        env->GetDirectBufferAddress(cache->modelViewBuffer));
    const auto* const projection = static_cast<const float*>(
        env->GetDirectBufferAddress(cache->projectionBuffer));
    const auto* const viewport = static_cast<const jint*>(
        env->GetDirectBufferAddress(cache->viewportBuffer));
    if (modelView == nullptr || projection == nullptr || viewport == nullptr ||
        env->GetDirectBufferCapacity(cache->modelViewBuffer) < 16 ||
        env->GetDirectBufferCapacity(cache->projectionBuffer) < 16 ||
        env->GetDirectBufferCapacity(cache->viewportBuffer) < 4) {
        clearException(env);
        return;
    }
    std::copy_n(modelView, camera.modelView.size(), camera.modelView.begin());
    std::copy_n(projection, camera.projection.size(), camera.projection.begin());
    for (std::size_t index = 0U; index < camera.viewport.size(); ++index) {
        camera.viewport[index] = viewport[index];
    }

    const bool finiteMatrices = std::all_of(
        camera.modelView.begin(), camera.modelView.end(),
        [](const float value) noexcept { return std::isfinite(value); }) &&
        std::all_of(camera.projection.begin(), camera.projection.end(),
        [](const float value) noexcept { return std::isfinite(value); });
    const bool saneViewport = camera.viewport[2U] >= 1 && camera.viewport[2U] <= 32768 &&
                              camera.viewport[3U] >= 1 && camera.viewport[3U] <= 32768;
    const bool nonEmptyMatrices = std::abs(camera.modelView[15U]) > 0.000001F &&
                                  std::abs(camera.projection[0U]) > 0.000001F &&
                                  std::abs(camera.projection[5U]) > 0.000001F;
    camera.valid = finiteMatrices && saneViewport && nonEmptyMatrices &&
                   std::isfinite(camera.renderX) && std::isfinite(camera.renderY) &&
                   std::isfinite(camera.renderZ) && std::isfinite(camera.partialTicks) &&
                   camera.partialTicks >= 0.0F && camera.partialTicks <= 1.5F;
    m_snapshot.camera = camera;
}

void GameBindings::sampleBow(JNIEnv* env, GameSnapshot& snapshot, bool enabled) noexcept
{
    snapshot.bowTrajectory = {};
    const BindingCache* c=m_cache.get();
    if(!enabled || !env || !c || !c->isMainThread || snapshot.state!=GameSnapshot::State::Ready ||
       !snapshot.camera.valid || !c->rayTraceBlocks || !c->rayVectorClass ||
       !c->rayVectorConstructor || !c->hitVector || !c->getItemUseDuration ||
       !c->isUsingItem || !c->getEyeHeight || !c->rotationYaw || !c->rotationPitch ||
       !c->getEquipmentInSlot || !c->getItem || !c->getIdFromItem || !c->itemClass ||
       std::any_of(c->vectorFields.begin(),c->vectorFields.end(),[](jfieldID f){return !f;})) return;
    if(env->PushLocalFrame(24)<0) { clearException(env); return; }
    const auto done=[&] { clearException(env); env->PopLocalFrame(nullptr); };
    jobject mc=c->minecraftInstanceField ? env->GetStaticObjectField(c->minecraftClass,c->minecraftInstanceField)
        : env->CallStaticObjectMethod(c->minecraftClass,c->getMinecraft);
    if(!mc || env->ExceptionCheck() || !env->CallBooleanMethod(mc,c->isMainThread)) { done(); return; }
    jobject player=env->GetObjectField(mc,c->playerField);
    jobject world=env->GetObjectField(mc,c->worldField);
    if(!player || !world || env->ExceptionCheck() || !env->CallBooleanMethod(player,c->isUsingItem)) { done(); return; }
    jobject stack=env->CallObjectMethod(player,c->getEquipmentInSlot,0);
    jobject item=stack && !env->ExceptionCheck() ? env->CallObjectMethod(stack,c->getItem) : nullptr;
    if(!item || env->ExceptionCheck() || env->CallStaticIntMethod(c->itemClass,c->getIdFromItem,item)!=261) { done(); return; }
    const int ticks=env->CallIntMethod(player,c->getItemUseDuration);
    const double charge=trajectory::bowStrength(static_cast<double>(ticks)+snapshot.camera.partialTicks);
    const double eye=env->CallFloatMethod(player,c->getEyeHeight);
    constexpr double radians=3.14159265358979323846/180;
    const double yaw=env->GetFloatField(player,c->rotationYaw)*radians;
    const double pitch=env->GetFloatField(player,c->rotationPitch)*radians;
    if(env->ExceptionCheck() || charge<0.1 || !std::isfinite(yaw) || !std::isfinite(pitch) ||
       !std::isfinite(eye)) { done(); return; }
    // Same interpolated origin as the current rendered world, with the vanilla
    // bow's lateral 0.16 and vertical 0.1 offsets. No visual easing of the path.
    WorldPoint position{snapshot.camera.renderX-std::cos(yaw)*0.16,
                        snapshot.camera.renderY+eye-0.1,
                        snapshot.camera.renderZ-std::sin(yaw)*0.16};
    WorldPoint velocity{-std::sin(yaw)*std::cos(pitch)*charge*3,
                        -std::sin(pitch)*charge*3,
                        std::cos(yaw)*std::cos(pitch)*charge*3};
    BowTrajectory result; result.active=true;
    result.points[result.pointCount++]=position;
    const auto started=std::chrono::steady_clock::now();
    for(std::size_t step=1;step<result.points.size();++step) {
        const WorldPoint next{position.x+velocity.x,position.y+velocity.y,position.z+velocity.z};
        double closest=std::numeric_limits<double>::infinity();
        const EntityMarker* hitEntity=nullptr;
        for(std::size_t i=0;i<std::min<std::size_t>(snapshot.entityMarkerCount,snapshot.entityMarkers.size());++i) {
            const auto& marker=snapshot.entityMarkers[i];
            if(marker.entityId==snapshot.entityId || !std::isfinite(marker.health) || marker.health<=0 || marker.fireball) continue;
            const double t=snapshot.entityRenderTick;
            const WorldPoint offset{marker.previousX+(marker.currentX-marker.previousX)*t-marker.currentX,
                                    marker.previousY+(marker.currentY-marker.previousY)*t-marker.currentY,
                                    marker.previousZ+(marker.currentZ-marker.previousZ)*t-marker.currentZ};
            auto box=marker.bounds;
            box.minX+=offset.x; box.maxX+=offset.x; box.minY+=offset.y; box.maxY+=offset.y;
            box.minZ+=offset.z; box.maxZ+=offset.z;
            const double fraction=trajectory::segmentBox(position,next,box,0.3);
            if(fraction<closest) { closest=fraction; hitEntity=&marker; }
        }
        // Vanilla rayTraceBlocks respects slabs, fences and non-colliding
        // blocks; a solid-voxel test cannot produce the same impact point.
        if(env->PushLocalFrame(8)<0) { result={}; break; }
        jobject from=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,position.x,position.y,position.z);
        jobject to=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,next.x,next.y,next.z);
        jobject blockHit=nullptr;
        if(from && to && !env->ExceptionCheck())
            blockHit=env->CallObjectMethod(world,c->rayTraceBlocks,from,to,JNI_FALSE,JNI_TRUE,JNI_FALSE);
        if(blockHit && !env->ExceptionCheck()) {
            jobject hit=env->GetObjectField(blockHit,c->hitVector);
            if(hit && !env->ExceptionCheck()) {
                WorldPoint point{env->GetDoubleField(hit,c->vectorFields[0]),env->GetDoubleField(hit,c->vectorFields[1]),
                                 env->GetDoubleField(hit,c->vectorFields[2])};
                const double length=trajectory::distanceSquared(position,next);
                const double fraction=length>1e-12 ? std::sqrt(trajectory::distanceSquared(position,point)/length) : 0;
                if(fraction<=closest) { closest=std::clamp(fraction,0.0,1.0); hitEntity=nullptr; }
            }
        }
        const bool failed=env->ExceptionCheck()==JNI_TRUE;
        clearException(env); env->PopLocalFrame(nullptr);
        if(failed) { result={}; break; }
        if(std::isfinite(closest)) {
            result.hasImpact=true; result.impactLiving=hitEntity!=nullptr;
            result.impactPlayer=hitEntity && hitEntity->player;
            result.impactEntityId=hitEntity ? hitEntity->entityId : -1;
            result.impact=trajectory::interpolate(position,next,closest);
            result.points[result.pointCount++]=result.impact;
            break;
        }
        result.points[result.pointCount++]=next; position=next;
        velocity.x*=0.99; velocity.y=velocity.y*0.99-0.05; velocity.z*=0.99;
        if(std::chrono::steady_clock::now()-started>std::chrono::microseconds(1500)) {
            // Do not invent a landing point if this frame exhausts the budget.
            result.budgetLimited=true; break;
        }
    }
    snapshot.bowTrajectory=result;
    done();
}

const GameSnapshot& GameBindings::sample(JNIEnv* const env,
                                         const std::uint64_t tickMilliseconds) noexcept
{
    // Minecraft simulation runs at 20 TPS. Sampling once per tick keeps the
    // JNI budget bounded while the renderer uses last/current positions plus
    // renderPartialTicks to produce frame-rate-smooth hitboxes.
    constexpr std::uint64_t kSampleIntervalMs = 50U;
    if (env == nullptr ||
        m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved) {
        return m_snapshot;
    }
    BindingCache* const cache = m_cache.get();
    if (cache == nullptr || cache->profile == nullptr) {
        m_snapshot.state = GameSnapshot::State::JniError;
        return m_snapshot;
    }

    if (tickMilliseconds - m_lastSample < kSampleIntervalMs && m_lastSample != 0U) {
        return m_snapshot;
    }
    m_lastSample = tickMilliseconds;

    if (env->PushLocalFrame(256) < 0) {
        clearException(env);
        m_snapshot.state = GameSnapshot::State::JniError;
        return m_snapshot;
    }

    auto failJni = [&]() noexcept -> const GameSnapshot& {
        clearException(env);
        env->PopLocalFrame(nullptr);
        m_snapshot.state = GameSnapshot::State::JniError;
        return m_snapshot;
    };

    jobject minecraft = cache->minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    if (minecraft == nullptr) {
        env->PopLocalFrame(nullptr);
        m_snapshot.state = GameSnapshot::State::WaitingForGameThread;
        return m_snapshot;
    }
    const jboolean onMainThread = env->CallBooleanMethod(minecraft, cache->isMainThread);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    if (onMainThread != JNI_TRUE) {
        env->PopLocalFrame(nullptr);
        // SwapBuffers can also belong to Forge's splash renderer. Never read
        // world state from that thread/context.
        m_snapshot.state = GameSnapshot::State::WaitingForGameThread;
        return m_snapshot;
    }

    m_snapshot.hypixelServer = false;
    if (cache->getCurrentServerData != nullptr && cache->serverIp != nullptr) {
        jobject serverData = env->CallObjectMethod(
            minecraft, cache->getCurrentServerData);
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
        } else if (serverData != nullptr) {
            jstring address = static_cast<jstring>(
                env->GetObjectField(serverData, cache->serverIp));
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
            } else if (address != nullptr) {
                const char* utf = env->GetStringUTFChars(address, nullptr);
                if (utf != nullptr) {
                    std::string host(utf);
                    env->ReleaseStringUTFChars(address, utf);
                    std::transform(host.begin(), host.end(), host.begin(),
                        [](const unsigned char value) noexcept {
                            return static_cast<char>(std::tolower(value));
                        });
                    const std::size_t colon = host.find(':');
                    if (colon != std::string::npos) host.resize(colon);
                    m_snapshot.hypixelServer = host == "hypixel.net" ||
                        (host.size() > 12U && host.ends_with(".hypixel.net"));
                }
            }
        }
    }

    jobject player = env->GetObjectField(minecraft, cache->playerField);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    jobject world = env->GetObjectField(minecraft, cache->worldField);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    if (player == nullptr || world == nullptr) {
        if (m_lastWorld != nullptr) {
            env->DeleteWeakGlobalRef(m_lastWorld);
            m_lastWorld = nullptr;
        }
        m_sidebarCandidateTeam = bedwars::Team::Unknown;
        m_sidebarStableCount = 0U;
        m_sidebarMissingCount = 0U;
        if (!(m_matchProbe == MatchProbeState{})) {
            m_matchProbe = {};
            ++m_matchProbeGeneration;
        }
        m_matchAnchorValid = false;
        m_lockedOwnBedKnown = false;
        m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        if (m_snapshot.matchActive || m_snapshot.playerCount != 0U ||
            m_snapshot.ownTeam != 'u' ||
            m_snapshot.localPlayerName[0U] != '\0') {
            m_snapshot.matchActive = false;
            m_snapshot.playerCount = 0U;
            m_snapshot.players = {};
            m_snapshot.localPlayerName = {};
            m_snapshot.ownTeam = 'u';
            m_snapshot.ownBedKnown = false;
            m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
            m_snapshot.playerRosterGeneration = ++m_playerRosterGeneration;
        }
        env->PopLocalFrame(nullptr);
        m_snapshot.state = GameSnapshot::State::NoPlayer;
        return m_snapshot;
    }

    // A WorldClient identity change is the hard lifecycle boundary for every
    // match-derived value. No translated server text or /rejoin command needs
    // to be recognized, and stale team/bed state cannot leak into the next map.
    if (m_lastWorld == nullptr || env->IsSameObject(m_lastWorld, world) != JNI_TRUE) {
        m_snapshot.worldGeneration = ++m_worldGeneration;
        m_knockbackTracks = {};
        m_snapshot.knockbackDamageEvents = m_snapshot.knockbackImpulseEvents =
            m_snapshot.knockbackConfirmedEvents = 0;
        if (m_lastWorld != nullptr) env->DeleteWeakGlobalRef(m_lastWorld);
        m_lastWorld = env->NewWeakGlobalRef(world);
        m_sidebarCandidateTeam = bedwars::Team::Unknown;
        m_sidebarStableCount = 0U;
        m_sidebarMissingCount = 0U;
        m_matchProbe = {};
        ++m_matchProbeGeneration;
        m_matchAnchorValid = false;
        m_lockedOwnBedKnown = false;
        m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        m_snapshot.matchActive = false;
        m_snapshot.ownTeam = 'u';
        m_snapshot.ownBedKnown = false;
        m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
        m_snapshot.players = {};
        m_snapshot.playerCount = 0U;
        m_debugRosterGeneration = 0U;
        m_snapshot.playerRosterGeneration = ++m_playerRosterGeneration;
    }

    const jfloat health = env->CallFloatMethod(player, cache->getHealth);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jfloat maxHealth = env->CallFloatMethod(player, cache->getMaxHealth);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jint entityId = env->CallIntMethod(player, cache->getEntityId);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jdouble positionX = env->GetDoubleField(player, cache->positionX);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jdouble positionY = env->GetDoubleField(player, cache->positionY);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jdouble positionZ = env->GetDoubleField(player, cache->positionZ);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    jobject bounds = env->CallObjectMethod(player, cache->getBounds);
    if (env->ExceptionCheck() == JNI_TRUE || bounds == nullptr) return failJni();

    AxisAlignedBox box;
    box.minX = env->GetDoubleField(bounds, cache->minX);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.minY = env->GetDoubleField(bounds, cache->minY);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.minZ = env->GetDoubleField(bounds, cache->minZ);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.maxX = env->GetDoubleField(bounds, cache->maxX);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.maxY = env->GetDoubleField(bounds, cache->maxY);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.maxZ = env->GetDoubleField(bounds, cache->maxZ);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();

    jobject loadedEntities = cache->loadedEntitiesField != nullptr
        ? env->GetObjectField(world, cache->loadedEntitiesField)
        : env->CallObjectMethod(world, cache->getLoadedEntities);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jint loadedEntityCount = loadedEntities == nullptr
        ? 0 : env->CallIntMethod(loadedEntities, cache->listSize);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    jboolean integratedSinglePlayer = env->CallBooleanMethod(
        minecraft, cache->isSingleplayer);
    if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        integratedSinglePlayer = JNI_FALSE;
    }
    const jboolean singlePlayer = JNI_TRUE;
    if (!std::isfinite(health) || !std::isfinite(maxHealth)) return failJni();

    m_snapshot.singlePlayer = singlePlayer == JNI_TRUE;
    m_snapshot.integratedSinglePlayer = integratedSinglePlayer == JNI_TRUE;

    auto readEntityMarker = [&](jobject const entity, EntityMarker& marker,
                                const bool livingEntity) noexcept {
        marker = {};
        marker.entityId = env->CallIntMethod(entity, cache->getEntityId);
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            return false;
        }
        if (livingEntity) {
            marker.health = env->CallFloatMethod(entity, cache->getHealth);
            marker.maxHealth = env->CallFloatMethod(entity, cache->getMaxHealth);
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
                marker.health = 0.0F;
                marker.maxHealth = 0.0F;
            }
            if (cache->hurtTime != nullptr) {
                marker.hurtTime = env->GetIntField(entity, cache->hurtTime);
                if (env->ExceptionCheck()) { clearException(env); marker.hurtTime = -1; }
            }
        }
        if (cache->isInvisible != nullptr) {
            marker.invisible = env->CallBooleanMethod(entity, cache->isInvisible) == JNI_TRUE;
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
                marker.invisible = false;
            }
        }
        jobject entityBounds = env->CallObjectMethod(entity, cache->getBounds);
        if (env->ExceptionCheck() == JNI_TRUE || entityBounds == nullptr) {
            env->ExceptionClear();
            return false;
        }
        marker.bounds.minX = env->GetDoubleField(entityBounds, cache->minX);
        marker.bounds.minY = env->GetDoubleField(entityBounds, cache->minY);
        marker.bounds.minZ = env->GetDoubleField(entityBounds, cache->minZ);
        marker.bounds.maxX = env->GetDoubleField(entityBounds, cache->maxX);
        marker.bounds.maxY = env->GetDoubleField(entityBounds, cache->maxY);
        marker.bounds.maxZ = env->GetDoubleField(entityBounds, cache->maxZ);
        marker.currentX = env->GetDoubleField(entity, cache->positionX);
        marker.currentY = env->GetDoubleField(entity, cache->positionY);
        marker.currentZ = env->GetDoubleField(entity, cache->positionZ);
        marker.previousX = env->GetDoubleField(entity, cache->previousPosition[0U]);
        marker.previousY = env->GetDoubleField(entity, cache->previousPosition[1U]);
        marker.previousZ = env->GetDoubleField(entity, cache->previousPosition[2U]);
        if (cache->motionFields[0U] != nullptr &&
            cache->motionFields[1U] != nullptr &&
            cache->motionFields[2U] != nullptr) {
            marker.motionX = env->GetDoubleField(entity, cache->motionFields[0U]);
            marker.motionY = env->GetDoubleField(entity, cache->motionFields[1U]);
            marker.motionZ = env->GetDoubleField(entity, cache->motionFields[2U]);
        }
        if (cache->onGround != nullptr) {
            marker.onGround = env->GetBooleanField(entity, cache->onGround) == JNI_TRUE;
            marker.groundKnown = !env->ExceptionCheck();
        }
        const bool failed = env->ExceptionCheck() == JNI_TRUE;
        if (failed) env->ExceptionClear();
        env->DeleteLocalRef(entityBounds);
        if (failed) return false;
        const double centerEntityX = (marker.bounds.minX + marker.bounds.maxX) * 0.5;
        const double centerEntityY = (marker.bounds.minY + marker.bounds.maxY) * 0.5;
        const double centerEntityZ = (marker.bounds.minZ + marker.bounds.maxZ) * 0.5;
        const double deltaX = centerEntityX - positionX;
        const double deltaY = centerEntityY - positionY;
        const double deltaZ = centerEntityZ - positionZ;
        marker.distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        return std::isfinite(marker.distance) &&
               std::isfinite(marker.currentX) && std::isfinite(marker.currentY) &&
               std::isfinite(marker.currentZ) && std::isfinite(marker.previousX) &&
               std::isfinite(marker.previousY) && std::isfinite(marker.previousZ) &&
               std::isfinite(marker.motionX) && std::isfinite(marker.motionY) &&
               std::isfinite(marker.motionZ) &&
               std::isfinite(marker.bounds.minX) && std::isfinite(marker.bounds.minY) &&
               std::isfinite(marker.bounds.minZ) && std::isfinite(marker.bounds.maxX) &&
               std::isfinite(marker.bounds.maxY) && std::isfinite(marker.bounds.maxZ);
    };

    // Read the actual dyed leather chestplate data, never the rendered/glint
    // colour. The same routine is used for the local player (match fallback)
    // and remote players (teammate/threat classification), which prevents the
    // two paths from drifting apart.
    auto readPlayerArmorTeam = [&](jobject const playerObject,
                                   bool& chestplatePresent,
                                   std::uint8_t& protectionLevel) noexcept {
        chestplatePresent = false;
        protectionLevel = 0U;
        if (playerObject == nullptr || cache->getEquipmentInSlot == nullptr ||
            cache->getItem == nullptr ||
            cache->itemArmorClass == nullptr || cache->hasColor == nullptr ||
            cache->getColor == nullptr) {
            return bedwars::Team::Unknown;
        }

        bedwars::Team result = bedwars::Team::Unknown;
        // Remote inventories are private server state. S04PacketEntityEquipment
        // is instead applied to EntityLivingBase's public equipment slots, so
        // getEquipmentInSlot is the authoritative path for other players.
        // Slot layout in 1.8.9 is 0=held, 1=boots, 2=leggings,
        // 3=chestplate, 4=helmet.
        jobject chestplate = env->CallObjectMethod(
            playerObject, cache->getEquipmentInSlot, 3);
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            chestplate = nullptr;
        }
        if (chestplate != nullptr) {
            chestplatePresent = true;
            jobject item = env->CallObjectMethod(chestplate, cache->getItem);
            if (env->ExceptionCheck() != JNI_TRUE && item != nullptr &&
                env->IsInstanceOf(item, cache->itemArmorClass) == JNI_TRUE) {
                const jboolean coloured = env->CallBooleanMethod(
                    item, cache->hasColor, chestplate);
                if (env->ExceptionCheck() != JNI_TRUE && coloured == JNI_TRUE) {
                    const jint rgb = env->CallIntMethod(item, cache->getColor, chestplate);
                    if (env->ExceptionCheck() != JNI_TRUE) {
                        result = bedwars::fromLeatherRgb(
                            static_cast<std::uint32_t>(rgb));
                    }
                }
            }
            clearException(env);
            if (item != nullptr) env->DeleteLocalRef(item);
        }

        if (cache->enchantmentHelperClass != nullptr &&
            cache->getEnchantmentLevel != nullptr) {
            // Protection can be carried by any visible armour piece. Query all
            // four packet-backed slots and keep the strongest level instead of
            // assuming that a transformed client mirrors the chestplate into
            // InventoryPlayer. Protection's legacy enchantment ID is 0.
            for (jint slot = 1; slot <= 4; ++slot) {
                jobject armorStack = env->CallObjectMethod(
                    playerObject, cache->getEquipmentInSlot, slot);
                if (env->ExceptionCheck() == JNI_TRUE) {
                    clearException(env);
                    armorStack = nullptr;
                }
                if (armorStack != nullptr) {
                    const jint level = env->CallStaticIntMethod(
                        cache->enchantmentHelperClass,
                        cache->getEnchantmentLevel, 0, armorStack);
                    if (env->ExceptionCheck() != JNI_TRUE) {
                        protectionLevel = std::max(
                            protectionLevel,
                            static_cast<std::uint8_t>(std::clamp(level, 0, 10)));
                    }
                    clearException(env);
                    env->DeleteLocalRef(armorStack);
                }
            }
        }
        if (chestplate != nullptr) env->DeleteLocalRef(chestplate);
        clearException(env);
        return result;
    };

    auto copyUuidString = [&](jobject const uuidObject,
                              std::array<char, 37U>& destination) noexcept {
        if (uuidObject == nullptr || cache->uuidToString == nullptr) return false;
        jstring text = static_cast<jstring>(
            env->CallObjectMethod(uuidObject, cache->uuidToString));
        if (env->ExceptionCheck() == JNI_TRUE || text == nullptr) {
            clearException(env);
            if (text != nullptr) env->DeleteLocalRef(text);
            return false;
        }
        bool copied = false;
        const char* const utf8 = env->GetStringUTFChars(text, nullptr);
        if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
            const std::string_view value(utf8);
            if (value.size() == 36U) {
                std::copy(value.begin(), value.end(), destination.begin());
                copied = true;
            }
        }
        if (utf8 != nullptr) env->ReleaseStringUTFChars(text, utf8);
        clearException(env);
        env->DeleteLocalRef(text);
        return copied;
    };

    jobject textureManagerObject = nullptr;
    if (cache->getTextureManager != nullptr) {
        textureManagerObject = env->CallObjectMethod(
            minecraft, cache->getTextureManager);
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            textureManagerObject = nullptr;
        }
    }

    auto readSkinTextureId = [&](jobject const playerObject) noexcept -> std::uint32_t {
        if (playerObject == nullptr || textureManagerObject == nullptr ||
            cache->abstractClientPlayerClass == nullptr ||
            cache->getLocationSkin == nullptr || cache->getTexture == nullptr ||
            cache->getGlTextureId == nullptr ||
            env->IsInstanceOf(playerObject, cache->abstractClientPlayerClass) != JNI_TRUE) {
            clearException(env);
            return 0U;
        }
        jobject location = env->CallObjectMethod(playerObject, cache->getLocationSkin);
        jobject texture = nullptr;
        if (env->ExceptionCheck() != JNI_TRUE && location != nullptr) {
            texture = env->CallObjectMethod(textureManagerObject,
                                             cache->getTexture, location);
        }
        jint textureId = 0;
        if (env->ExceptionCheck() != JNI_TRUE && texture != nullptr) {
            textureId = env->CallIntMethod(texture, cache->getGlTextureId);
        }
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            textureId = 0;
        }
        if (texture != nullptr) env->DeleteLocalRef(texture);
        if (location != nullptr) env->DeleteLocalRef(location);
        return textureId > 0 ? static_cast<std::uint32_t>(textureId) : 0U;
    };

    // Collect live world entities on all supported servers. Player entity type
    // and nickname do not depend on online authentication or a TAB roster join.
    // A single List.toArray() avoids one virtual JNI call per list index. The
    // fixed 128-marker cap and 20 Hz cadence are both deterministic; smooth
    // motion is reconstructed in OverlayRenderer from previous/current tick
    // coordinates and the per-frame Timer.renderPartialTicks value.
    // Knockback evidence keeps a bounded per-identity history so asynchronous
    // hurt/status and movement updates can be correlated across snapshots.
    if (singlePlayer == JNI_TRUE && loadedEntities != nullptr && loadedEntityCount > 0) {
        jobject playerList = env->GetObjectField(world, cache->playerEntities);
        jobjectArray playerObjects = playerList == nullptr ? nullptr :
            static_cast<jobjectArray>(env->CallObjectMethod(playerList, cache->listToArray));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            playerObjects = nullptr;
        }
        jobjectArray entities = static_cast<jobjectArray>(
            env->CallObjectMethod(loadedEntities, cache->listToArray));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            entities = nullptr;
        }
        m_snapshot.entityMarkerCount = 0U;
        if (entities != nullptr) {
            const jsize entityLimit = std::min<jsize>(env->GetArrayLength(entities), 4096);
            // Real players take priority in bounded marker storage; a server's
            // decorative mobs must not crowd them out before target selection.
            for(int pass=0;pass<2;++pass)
            for (jsize index = 0; index < entityLimit &&
                 m_snapshot.entityMarkerCount < GameSnapshot::MaxEntityMarkers; ++index) {
                jobject entity = env->GetObjectArrayElement(entities, index);
                if (env->ExceptionCheck() == JNI_TRUE) {
                    env->ExceptionClear();
                    continue;
                }
                if (entity == nullptr) continue;
                const bool isLocalPlayer = env->IsSameObject(entity, player) == JNI_TRUE;
                const bool isPlayer=env->IsInstanceOf(entity,cache->playerClass)==JNI_TRUE;
                const bool isLiving = env->IsInstanceOf(entity, cache->livingClass) == JNI_TRUE;
                const bool isHostile = cache->hostileClass != nullptr &&
                    env->IsInstanceOf(entity, cache->hostileClass) == JNI_TRUE;
                const bool isFireball = cache->fireballClass != nullptr &&
                    env->IsInstanceOf(entity, cache->fireballClass) == JNI_TRUE;
                if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
                if (!isLocalPlayer && (isLiving || isFireball) && (pass==0?isPlayer:!isPlayer)) {
                    EntityMarker marker;
                    if (readEntityMarker(entity, marker, isLiving)) {
                        marker.fireball = isFireball;
                        marker.hostile = isHostile;
                        // Entity type is authoritative on offline/custom servers;
                        // a truncated TAB/world-list join must not exclude real players.
                        marker.player=isPlayer;
                        if (!marker.player && playerObjects != nullptr) {
                            const jsize playerLimit = std::min<jsize>(
                                env->GetArrayLength(playerObjects), 64);
                            for (jsize playerIndex = 0; playerIndex < playerLimit;
                                 ++playerIndex) {
                                jobject candidate = env->GetObjectArrayElement(
                                    playerObjects, playerIndex);
                                if (candidate != nullptr) {
                                    marker.player = env->IsSameObject(entity, candidate) == JNI_TRUE;
                                    env->DeleteLocalRef(candidate);
                                }
                                if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
                                if (marker.player) break;
                            }
                        }

                        if (marker.player) {
                            jstring markerName = static_cast<jstring>(
                                env->CallObjectMethod(entity, cache->getName));
                            if (env->ExceptionCheck() != JNI_TRUE && markerName != nullptr) {
                                const char* const utf8 = env->GetStringUTFChars(markerName, nullptr);
                                if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
                                    const std::string_view nameView(utf8);
                                    std::size_t displayLength=std::min(nameView.size(),marker.displayName.size()-1U);
                                    if(displayLength<nameView.size())
                                        while(displayLength&&
                                            (static_cast<unsigned char>(nameView[displayLength])&0xC0U)==0x80U) --displayLength;
                                    std::copy_n(nameView.data(),displayLength,marker.displayName.data());
                                    if (!nameView.empty() && nameView.size() <= 16U) {
                                        std::copy(nameView.begin(), nameView.end(),
                                                  marker.playerName.begin());
                                    }
                                }
                                if (utf8 != nullptr) env->ReleaseStringUTFChars(markerName, utf8);
                            }
                            clearException(env);
                            if (markerName != nullptr) env->DeleteLocalRef(markerName);

                            if (cache->getUniqueId != nullptr) {
                                jobject uuidObject = env->CallObjectMethod(
                                    entity, cache->getUniqueId);
                                if (env->ExceptionCheck() != JNI_TRUE && uuidObject != nullptr) {
                                    (void)copyUuidString(uuidObject, marker.uuid);
                                }
                                clearException(env);
                                if (uuidObject != nullptr) env->DeleteLocalRef(uuidObject);
                            }
                            marker.skinTextureId = readSkinTextureId(entity);
                        }

                        if (marker.player) {
                            const bedwars::Team armorTeam =
                                readPlayerArmorTeam(entity, marker.hasArmor,
                                                    marker.protectionLevel);
                            marker.armorTeam = bedwars::formatCode(armorTeam);
                            if (cache->getEquipmentInSlot != nullptr &&
                                cache->getIdFromItem != nullptr &&
                                cache->stackSize != nullptr &&
                                cache->getItemDamage != nullptr) {
                                jobject heldStack = env->CallObjectMethod(
                                    entity, cache->getEquipmentInSlot, 0);
                                if (env->ExceptionCheck() != JNI_TRUE && heldStack != nullptr) {
                                    jobject heldItem = env->CallObjectMethod(
                                        heldStack, cache->getItem);
                                    if (env->ExceptionCheck() != JNI_TRUE && heldItem != nullptr) {
                                        const jint itemId = env->CallStaticIntMethod(
                                            cache->itemClass, cache->getIdFromItem, heldItem);
                                        const jint count = env->GetIntField(
                                            heldStack, cache->stackSize);
                                        const jint damage = env->CallIntMethod(
                                            heldStack, cache->getItemDamage);
                                        if (env->ExceptionCheck() != JNI_TRUE) {
                                            marker.heldItemId = static_cast<std::int16_t>(
                                                std::clamp(itemId, -1, 32767));
                                            marker.heldItemCount = static_cast<std::uint8_t>(
                                                std::clamp(count, 0, 255));
                                            marker.heldItemDamage = static_cast<std::uint16_t>(
                                                std::clamp(damage, 0, 65535));
                                        }
                                    }
                                    clearException(env);
                                    if (heldItem != nullptr) env->DeleteLocalRef(heldItem);
                                    env->DeleteLocalRef(heldStack);
                                } else {
                                    clearException(env);
                                }
                            }
                        }

                        marker.teamColor = marker.armorTeam;
                        if (marker.playerName[0U] != '\0' || marker.uuid[0U] != '\0') {
                            for (std::uint32_t rosterIndex = 0U;
                                 rosterIndex < m_snapshot.playerCount; ++rosterIndex) {
                                const PlayerIdentity& identity = m_snapshot.players[rosterIndex];
                                const bool uuidMatch = marker.uuid[0U] != '\0' &&
                                    identity.uuid[0U] != '\0' &&
                                    std::strcmp(identity.uuid.data(), marker.uuid.data()) == 0;
                                const bool nameMatch = marker.playerName[0U] != '\0' &&
                                    std::strcmp(identity.name.data(),
                                                marker.playerName.data()) == 0;
                                if (uuidMatch || nameMatch) {
                                    marker.teamColor = identity.teamColor;
                                    marker.confirmedPlayer = true;
                                    // TAB owns the player-facing nickname.
                                    // Use it for alerts/nametags after the UUID
                                    // join instead of exposing Hypixel's entity
                                    // alias when the two strings differ.
                                    std::copy(identity.name.begin(), identity.name.end(),
                                              marker.playerName.begin());
                                    marker.displayName={};
                                    std::copy(identity.name.begin(),identity.name.end(),marker.displayName.begin());
                                    break;
                                }
                            }
                        }
                        
                        m_snapshot.entityMarkers[m_snapshot.entityMarkerCount++] = marker;
                    }
                }
                env->DeleteLocalRef(entity);
            }
            env->DeleteLocalRef(entities);
        }
        if (playerObjects != nullptr) env->DeleteLocalRef(playerObjects);
        if (playerList != nullptr) env->DeleteLocalRef(playerList);
    } else {
        m_snapshot.entityMarkerCount = 0U;
    }

    // Knockback evidence stays on the bounded 20 TPS sampler. Bow aiming is
    // different: sampleBow traces the current view once per presented frame.
    m_snapshot.knockbackTrajectoryCount = 0U;
    m_snapshot.knockbackHurtAvailable = cache->hurtTime != nullptr;
    const bool trajectoryBlocksAvailable = cache->isAirBlock != nullptr &&
        cache->blockPosClass != nullptr && cache->blockPosConstructor != nullptr;
    const auto blockIsAir = [&](const double x, const double y,
                                const double z, bool& air) noexcept {
        if (!trajectoryBlocksAvailable) return false;
        jobject position = env->NewObject(cache->blockPosClass,
            cache->blockPosConstructor,
            static_cast<jint>(std::floor(x)),
            static_cast<jint>(std::floor(y)),
            static_cast<jint>(std::floor(z)));
        if (position == nullptr || env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            return false;
        }
        air = env->CallBooleanMethod(world, cache->isAirBlock, position) == JNI_TRUE;
        const bool valid = env->ExceptionCheck() != JNI_TRUE;
        clearException(env);
        env->DeleteLocalRef(position);
        return valid;
    };

    if (trajectoryBlocksAvailable) {
        for (std::uint32_t markerIndex = 0U;
             markerIndex < m_snapshot.entityMarkerCount &&
             m_snapshot.knockbackTrajectoryCount <
                 GameSnapshot::MaxKnockbackTrajectories; ++markerIndex) {
            const EntityMarker& marker = m_snapshot.entityMarkers[markerIndex];
            if (!marker.player || (m_snapshot.hypixelServer && !marker.confirmedPlayer)) continue;
            auto trackIt = std::find_if(m_knockbackTracks.begin(), m_knockbackTracks.end(),
                [&](const KnockbackTrack& track) {
                    return track.entityId == marker.entityId &&
                        (marker.uuid[0] ? track.uuid == marker.uuid : track.name == marker.playerName);
                });
            if (trackIt == m_knockbackTracks.end()) {
                trackIt = std::min_element(m_knockbackTracks.begin(), m_knockbackTracks.end(),
                    [](const KnockbackTrack& a, const KnockbackTrack& b) { return a.lastSeen < b.lastSeen; });
                *trackIt = {};
                trackIt->entityId = marker.entityId;
                trackIt->uuid = marker.uuid; trackIt->name = marker.playerName;
            }
            auto& track = *trackIt;
            prediction::Velocity velocity{marker.motionX, marker.motionY, marker.motionZ};
            // EntityOtherPlayerMP may interpolate server positions without
            // useful motion fields. Use measured displacement only for a
            // recent sample; never extrapolate an unloaded/teleported player.
            const auto elapsed = tickMilliseconds - track.lastSeen;
            prediction::Velocity measured{};
            const bool measuredValid=track.lastSeen&&elapsed>=15&&elapsed<=150;
            if(measuredValid) {
                const double samplesPerTick=50.0/static_cast<double>(elapsed);
                measured={(marker.currentX-track.position.x)*samplesPerTick,
                          (marker.currentY-track.position.y)*samplesPerTick,
                          (marker.currentZ-track.position.z)*samplesPerTick};
            }
            if (measuredValid &&
                std::hypot(velocity.x, velocity.z) < 0.001 && std::abs(velocity.y) < 0.001) {
                velocity=measured;
            }
            track.position = {marker.currentX, marker.currentY, marker.currentZ};
            track.lastSeen = tickMilliseconds;
            const bool plausible = std::hypot(velocity.x, velocity.z) < 4.0 && std::abs(velocity.y) < 4.0;
            const auto evidence = track.evidence.update({marker.health, marker.hurtTime,
                marker.onGround, marker.groundKnown && plausible, velocity}, tickMilliseconds);
            m_snapshot.knockbackDamageEvents += evidence.damage ? 1U : 0U;
            m_snapshot.knockbackImpulseEvents += evidence.impulse ? 1U : 0U;
            if(evidence.triggered) {
                // One edge pair is enough to nominate a knockback event, but
                // not enough to extrapolate a remote interpolated player.
                track.pendingImpulse=velocity;
                track.pendingAt=tickMilliseconds;
                track.pendingPrediction=true;
                continue;
            }
            if(!track.pendingPrediction) continue;
            if(marker.onGround||tickMilliseconds<=track.pendingAt||
               tickMilliseconds-track.pendingAt>160U) {
                track.pendingPrediction=false;
                continue;
            }
            const prediction::VelocityConfidence confidence=measuredValid
                ?prediction::confirmVelocity(track.pendingImpulse,measured,elapsed)
                :prediction::VelocityConfidence{};
            if(!confidence.confident) {
                if(tickMilliseconds-track.pendingAt>=90U)
                    track.pendingPrediction=false;
                continue;
            }
            track.pendingPrediction=false;
            velocity=confidence.residual;
            ++m_snapshot.knockbackConfirmedEvents;
            KnockbackTrajectory prediction;
            prediction.entityId = marker.entityId;
            prediction.startBounds = marker.bounds;
            double simulatedX = marker.currentX;
            double simulatedY = marker.bounds.minY;
            double simulatedZ = marker.currentZ;
            double velocityX = velocity.x;
            double velocityY = velocity.y;
            double velocityZ = velocity.z;
            prediction.points[prediction.pointCount++] = {
                simulatedX, simulatedY, simulatedZ};
            const double halfWidth=std::clamp(
                (marker.bounds.maxX-marker.bounds.minX)*0.5,0.20,0.60);
            const double height=std::clamp(
                marker.bounds.maxY-marker.bounds.minY,0.6,2.4);
            for (std::size_t step = 1U;
                 step < std::min<std::size_t>(prediction.points.size(),7U); ++step) {
                simulatedX += velocityX;
                simulatedY += velocityY;
                simulatedZ += velocityZ;
                if(velocityY<=0.0) {
                    bool airBelow=true;
                    const double probeY=simulatedY-0.06;
                    if(blockIsAir(simulatedX,probeY,simulatedZ,airBelow)&&
                       !airBelow) {
                        const double blockTop=std::floor(probeY)+1.0;
                        if(simulatedY<=blockTop+0.16) {
                            simulatedY=blockTop;
                            prediction.points[prediction.pointCount++]={
                                simulatedX,simulatedY,simulatedZ};
                            prediction.landed=true;
                            break;
                        }
                    }
                }
                bool volumeClear=true;
                bool volumeKnown=true;
                constexpr std::array<double,3> verticalFractions{0.04,0.50,0.94};
                const std::array<double,2> horizontalOffsets{
                    -halfWidth*0.88,halfWidth*0.88};
                for(const double fraction:verticalFractions) {
                    for(const double offsetX:horizontalOffsets) {
                        for(const double offsetZ:horizontalOffsets) {
                            bool air=true;
                            if(!blockIsAir(simulatedX+offsetX,
                                simulatedY+height*fraction,simulatedZ+offsetZ,air))
                                volumeKnown=false;
                            else if(!air) volumeClear=false;
                        }
                    }
                }
                // Unknown or occupied volume ends the high-confidence horizon;
                // never draw through a wall or unloaded collision query.
                if(!volumeKnown||!volumeClear) break;
                prediction.points[prediction.pointCount++] = {
                    simulatedX, simulatedY, simulatedZ};

                // 1.8 living-entity airborne approximation. The visual is a
                // client prediction; server corrections naturally replace it
                // on the next immutable entity snapshot.
                velocityX *= 0.91;
                velocityZ *= 0.91;
                velocityY = (velocityY - 0.08) * 0.98;
            }
            if(prediction.pointCount>=2U)
                m_snapshot.knockbackTrajectories[
                    m_snapshot.knockbackTrajectoryCount++] = prediction;
        }
    }

    // Bow physics is sampled separately at render cadence, not this 20 TPS sampler.
    m_snapshot.entitySampleGeneration = ++m_entitySampleGeneration;

    // Multiplayer discovery is metadata-only and independent of ESP. At 2 Hz,
    // two stable Sidebar snapshots activate a match in about one second while
    // keeping all collection outside the per-frame renderer path.
    if (tickMilliseconds - m_lastPlayerScan >= 500U || m_lastPlayerScan == 0U) {
        m_lastPlayerScan = tickMilliseconds;
        const bool previousMatchActive = m_snapshot.matchActive;
        const char previousOwnTeam = m_snapshot.ownTeam;
        std::array<PlayerIdentity, GameSnapshot::MaxDiscoveredPlayers> nextPlayers{};
        std::array<std::string, GameSnapshot::MaxDiscoveredPlayers> rosterFormatted{};
        std::array<char, 17U> nextLocalName{};
        std::uint32_t nextCount = 0U;
        jstring localName = static_cast<jstring>(env->CallObjectMethod(player, cache->getName));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            localName = nullptr;
        }
        if (localName != nullptr) {
            const char* const localUtf8 = env->GetStringUTFChars(localName, nullptr);
            if (env->ExceptionCheck() != JNI_TRUE && localUtf8 != nullptr) {
                const std::string_view localView(localUtf8);
                if (!localView.empty() && localView.size() <= 16U) {
                    std::copy(localView.begin(), localView.end(), nextLocalName.begin());
                }
            }
            if (localUtf8 != nullptr) env->ReleaseStringUTFChars(localName, localUtf8);
            if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
            env->DeleteLocalRef(localName);
        }
        auto appendRosterPlayer = [&](const std::string_view nameView,
                                      const std::string_view formattedView,
                                      const std::array<char, 37U>& uuidValue) noexcept {
            const bool validName = !nameView.empty() && nameView.size() <= 16U &&
                std::all_of(nameView.begin(), nameView.end(), [](const char c) noexcept {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '_';
                });
            if (!validName || nextCount >= nextPlayers.size()) return;
            for (std::uint32_t existing = 0U; existing < nextCount; ++existing) {
                const bool sameUuid = uuidValue[0U] != '\0' &&
                    nextPlayers[existing].uuid[0U] != '\0' &&
                    std::strcmp(uuidValue.data(), nextPlayers[existing].uuid.data()) == 0;
                if (sameUuid || nameView == nextPlayers[existing].name.data()) return;
            }
            PlayerIdentity identity{};
            std::copy(nameView.begin(), nameView.end(), identity.name.begin());
            identity.uuid = uuidValue;
            identity.teamColor = 'u';
            rosterFormatted[nextCount].assign(formattedView);
            nextPlayers[nextCount++] = identity;
        };

        // Prefer the server-maintained TAB collection. Unlike playerEntities,
        // it contains roster members before their entity is spawned/tracked by
        // the client and remains available when they are far outside render
        // distance. Mid-match additions are picked up by this continuous 2 Hz
        // scan as well.
        bool tabRosterRead = false;
        if (cache->getNetHandler != nullptr && cache->getPlayerInfoMap != nullptr &&
            cache->getGameProfile != nullptr && cache->gameProfileGetName != nullptr) {
            jobject netHandler = env->CallObjectMethod(minecraft, cache->getNetHandler);
            jobject playerInfoMap = nullptr;
            jobjectArray playerInfos = nullptr;
            if (env->ExceptionCheck() != JNI_TRUE && netHandler != nullptr) {
                playerInfoMap = env->CallObjectMethod(netHandler, cache->getPlayerInfoMap);
            }
            if (env->ExceptionCheck() != JNI_TRUE && playerInfoMap != nullptr) {
                playerInfos = static_cast<jobjectArray>(env->CallObjectMethod(
                    playerInfoMap, cache->collectionToArray));
            }
            if (env->ExceptionCheck() != JNI_TRUE && playerInfos != nullptr) {
                const jsize count = std::min<jsize>(env->GetArrayLength(playerInfos),
                    static_cast<jsize>(nextPlayers.size()));
                for (jsize index = 0; index < count; ++index) {
                    jobject info = env->GetObjectArrayElement(playerInfos, index);
                    jobject profileObject = info == nullptr ? nullptr :
                        env->CallObjectMethod(info, cache->getGameProfile);
                    jstring name = profileObject == nullptr ? nullptr : static_cast<jstring>(
                        env->CallObjectMethod(profileObject, cache->gameProfileGetName));
                    std::array<char, 37U> profileUuid{};
                    if (profileObject != nullptr && cache->gameProfileGetId != nullptr) {
                        jobject uuidObject = env->CallObjectMethod(
                            profileObject, cache->gameProfileGetId);
                        if (env->ExceptionCheck() != JNI_TRUE && uuidObject != nullptr) {
                            (void)copyUuidString(uuidObject, profileUuid);
                        }
                        clearException(env);
                        if (uuidObject != nullptr) env->DeleteLocalRef(uuidObject);
                    }
                    if (env->ExceptionCheck() != JNI_TRUE && name != nullptr) {
                        const char* const utf8 = env->GetStringUTFChars(name, nullptr);
                        if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
                            appendRosterPlayer(utf8, {}, profileUuid);
                            env->ReleaseStringUTFChars(name, utf8);
                        }
                    }
                    clearException(env);
                    if (name != nullptr) env->DeleteLocalRef(name);
                    if (profileObject != nullptr) env->DeleteLocalRef(profileObject);
                    if (info != nullptr) env->DeleteLocalRef(info);
                }
                tabRosterRead = nextCount != 0U;
            }
            clearException(env);
            if (playerInfos != nullptr) env->DeleteLocalRef(playerInfos);
            if (playerInfoMap != nullptr) env->DeleteLocalRef(playerInfoMap);
            if (netHandler != nullptr) env->DeleteLocalRef(netHandler);
        }

        // Compatibility fallback for transformed clients without a usable TAB
        // mapping. This path sees only currently spawned player entities.
        if (!tabRosterRead) {
            jobject playerList = env->GetObjectField(world, cache->playerEntities);
            jobjectArray playerArray = playerList == nullptr ? nullptr :
                static_cast<jobjectArray>(env->CallObjectMethod(
                    playerList, cache->listToArray));
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
                playerArray = nullptr;
            }
            if (playerArray != nullptr) {
                const jsize count = std::min<jsize>(env->GetArrayLength(playerArray),
                    static_cast<jsize>(nextPlayers.size()));
                for (jsize index = 0; index < count; ++index) {
                    jobject remotePlayer = env->GetObjectArrayElement(playerArray, index);
                    if (env->ExceptionCheck() == JNI_TRUE || remotePlayer == nullptr) {
                        clearException(env);
                        continue;
                    }
                    jstring name = static_cast<jstring>(
                        env->CallObjectMethod(remotePlayer, cache->getName));
                    jobject display = env->CallObjectMethod(remotePlayer, cache->getDisplayName);
                    jstring formatted = display == nullptr ? nullptr : static_cast<jstring>(
                        env->CallObjectMethod(display, cache->getFormattedText));
                    if (env->ExceptionCheck() != JNI_TRUE && name != nullptr) {
                        const char* const nameUtf8 = env->GetStringUTFChars(name, nullptr);
                        const char* const formattedUtf8 = formatted == nullptr ? nullptr :
                            env->GetStringUTFChars(formatted, nullptr);
                        if (env->ExceptionCheck() != JNI_TRUE && nameUtf8 != nullptr) {
                            std::array<char, 37U> entityUuid{};
                            if (cache->getUniqueId != nullptr) {
                                jobject uuidObject = env->CallObjectMethod(
                                    remotePlayer, cache->getUniqueId);
                                if (env->ExceptionCheck() != JNI_TRUE && uuidObject != nullptr) {
                                    (void)copyUuidString(uuidObject, entityUuid);
                                }
                                clearException(env);
                                if (uuidObject != nullptr) env->DeleteLocalRef(uuidObject);
                            }
                            appendRosterPlayer(nameUtf8,
                                formattedUtf8 == nullptr ? std::string_view{} :
                                std::string_view(formattedUtf8), entityUuid);
                        }
                        if (formattedUtf8 != nullptr)
                            env->ReleaseStringUTFChars(formatted, formattedUtf8);
                        if (nameUtf8 != nullptr)
                            env->ReleaseStringUTFChars(name, nameUtf8);
                    }
                    clearException(env);
                    if (formatted != nullptr) env->DeleteLocalRef(formatted);
                    if (display != nullptr) env->DeleteLocalRef(display);
                    if (name != nullptr) env->DeleteLocalRef(name);
                    env->DeleteLocalRef(remotePlayer);
                }
                env->DeleteLocalRef(playerArray);
            }
            if (playerList != nullptr) env->DeleteLocalRef(playerList);
        }
        std::array<std::string, 32U> sidebarStorage{};
        std::array<std::string_view, 32U> sidebarViews{};
        std::size_t sidebarLineCount = 0U;
        const bool sidebarAvailable =
            cache->getScoreboard != nullptr &&
            cache->getObjectiveInDisplaySlot != nullptr &&
            cache->getPlayersTeam != nullptr &&
            cache->getSortedScores != nullptr &&
            cache->getPlayerName != nullptr &&
            cache->formatPlayerName != nullptr &&
            cache->scorePlayerTeamClass != nullptr;
        jobject scoreboard = sidebarAvailable
            ? env->CallObjectMethod(world, cache->getScoreboard)
            : nullptr;
        if (env->ExceptionCheck() != JNI_TRUE && scoreboard != nullptr) {
            // TAB's GameProfile only carries the raw account name. Resolve its
            // current team through the world scoreboard and format it exactly
            // as Minecraft does; entries without a recognised colour/tag are
            // intentionally excluded later (lobby NPCs and start robots).
            for (std::uint32_t index = 0U; index < nextCount; ++index) {
                if (!rosterFormatted[index].empty()) continue;
                jstring name = env->NewStringUTF(nextPlayers[index].name.data());
                jobject team = name == nullptr ? nullptr : env->CallObjectMethod(
                    scoreboard, cache->getPlayersTeam, name);
                jstring formatted = team == nullptr ? nullptr : static_cast<jstring>(
                    env->CallStaticObjectMethod(cache->scorePlayerTeamClass,
                        cache->formatPlayerName, team, name));
                if (env->ExceptionCheck() != JNI_TRUE && formatted != nullptr) {
                    const char* const utf8 = env->GetStringUTFChars(formatted, nullptr);
                    if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
                        rosterFormatted[index] = utf8;
                        env->ReleaseStringUTFChars(formatted, utf8);
                    }
                }
                clearException(env);
                if (formatted != nullptr) env->DeleteLocalRef(formatted);
                if (team != nullptr) env->DeleteLocalRef(team);
                if (name != nullptr) env->DeleteLocalRef(name);
            }
            jobject objective = env->CallObjectMethod(scoreboard, cache->getObjectiveInDisplaySlot, 1);
            if (env->ExceptionCheck() != JNI_TRUE && objective != nullptr) {
                jobject scores = env->CallObjectMethod(scoreboard, cache->getSortedScores, objective);
                if (env->ExceptionCheck() != JNI_TRUE && scores != nullptr) {
                    jobjectArray scoresArray = static_cast<jobjectArray>(env->CallObjectMethod(
                        scores, cache->collectionToArray));
                    if (env->ExceptionCheck() != JNI_TRUE && scoresArray != nullptr) {
                        const jsize len = std::min<jsize>(
                            env->GetArrayLength(scoresArray),
                            static_cast<jsize>(sidebarStorage.size()));
                        for (jsize i = 0; i < len; i++) {
                            jobject scoreObj = env->GetObjectArrayElement(scoresArray, i);
                            if (env->ExceptionCheck() != JNI_TRUE && scoreObj != nullptr) {
                                jstring playerNameStr = static_cast<jstring>(env->CallObjectMethod(scoreObj, cache->getPlayerName));
                                if (env->ExceptionCheck() != JNI_TRUE && playerNameStr != nullptr) {
                                    jobject team = env->CallObjectMethod(scoreboard, cache->getPlayersTeam, playerNameStr);
                                    if (env->ExceptionCheck() != JNI_TRUE && team != nullptr) {
                                        jstring formattedStr = static_cast<jstring>(env->CallStaticObjectMethod(cache->scorePlayerTeamClass, cache->formatPlayerName, team, playerNameStr));
                                        if (env->ExceptionCheck() != JNI_TRUE && formattedStr != nullptr) {
                                            const char* formattedUtf8 = env->GetStringUTFChars(formattedStr, nullptr);
                                            if (formattedUtf8 != nullptr) {
                                                if (sidebarLineCount < sidebarStorage.size()) {
                                                    sidebarStorage[sidebarLineCount] = formattedUtf8;
                                                    sidebarViews[sidebarLineCount] =
                                                        sidebarStorage[sidebarLineCount];
                                                    ++sidebarLineCount;
                                                }
                                                env->ReleaseStringUTFChars(formattedStr, formattedUtf8);
                                            }
                                            env->DeleteLocalRef(formattedStr);
                                        }
                                        env->DeleteLocalRef(team);
                                    }
                                    env->DeleteLocalRef(playerNameStr);
                                }
                                env->DeleteLocalRef(scoreObj);
                            }
                        }
                        env->DeleteLocalRef(scoresArray);
                    }
                    env->DeleteLocalRef(scores);
                }
                env->DeleteLocalRef(objective);
            }
            env->DeleteLocalRef(scoreboard);
        }
        env->ExceptionClear();

        bool localChestplatePresent = false;
        std::uint8_t localProtectionLevel = 0U;
        const bedwars::Team localArmorTeam =
            readPlayerArmorTeam(player, localChestplatePresent,
                                localProtectionLevel);
        (void)localChestplatePresent;

        bedwars::Team rosterTaggedOwnTeam = bedwars::Team::Unknown;
        bedwars::Team rosterColourOwnTeam = bedwars::Team::Unknown;
        std::uint16_t rosterTeamMask = 0U;
        std::uint32_t taggedPlayers = 0U;
        for (std::uint32_t index = 0U; index < nextCount; ++index) {
            const bedwars::Team tagged =
                bedwars::parseRosterTeamTag(rosterFormatted[index]);
            const std::uint8_t teamIndex = bedwars::teamIndex(tagged);
            if (teamIndex >= 8U) continue;
            ++taggedPlayers;
            rosterTeamMask |= static_cast<std::uint16_t>(1U << teamIndex);
            if (nextLocalName[0U] != '\0' &&
                std::strcmp(nextPlayers[index].name.data(), nextLocalName.data()) == 0) {
                rosterTaggedOwnTeam = tagged;
                rosterColourOwnTeam =
                    bedwars::parseRosterTeam(rosterFormatted[index]);
            }
        }
        std::uint8_t rosterDistinctTeams = 0U;
        for (std::uint16_t mask = rosterTeamMask; mask != 0U; mask >>= 1U)
            rosterDistinctTeams += static_cast<std::uint8_t>(mask & 1U);
        // Restore the former roster detector: explicit [R]/[B]/... tags are
        // match evidence even when Lunar inserts an unrelated colour before
        // the tag. Prefer an explicit local tag, then the actual dyed local
        // chestplate, and only then the formatted-name colour.
        const bedwars::Team rosterOwnTeam =
            rosterTaggedOwnTeam != bedwars::Team::Unknown ? rosterTaggedOwnTeam :
            localArmorTeam != bedwars::Team::Unknown ? localArmorTeam :
            rosterColourOwnTeam;
        const bool rosterValid = taggedPlayers >= 2U &&
            rosterDistinctTeams >= 2U && rosterOwnTeam != bedwars::Team::Unknown;

        const bedwars::SidebarSnapshot sidebar = bedwars::parseSidebar(
            std::span<const std::string_view>(sidebarViews.data(), sidebarLineCount));

        // Last-resort match evidence for transformed clients whose scoreboard
        // wrappers remove both Sidebar suffixes and roster tags. It is accepted
        // only when (a) the local leather colour is known, (b) at least two
        // distinct live player armour teams are present, and (c) the world bed
        // scanner has found a bed. This avoids treating lobby rank colours as a
        // match while keeping team detection usable on Lunar.
        std::uint16_t armorTeamMask = 0U;
        const std::uint8_t localArmorIndex = bedwars::teamIndex(localArmorTeam);
        if (localArmorIndex < 8U)
            armorTeamMask |= static_cast<std::uint16_t>(1U << localArmorIndex);
        for (std::uint32_t index = 0U; index < m_snapshot.entityMarkerCount; ++index) {
            const EntityMarker& marker = m_snapshot.entityMarkers[index];
            if (!marker.player) continue;
            const std::uint8_t armorIndex = bedwars::teamIndex(
                bedwars::fromFormatCode(marker.armorTeam));
            if (armorIndex < 8U)
                armorTeamMask |= static_cast<std::uint16_t>(1U << armorIndex);
        }
        std::uint8_t armorDistinctTeams = 0U;
        for (std::uint16_t mask = armorTeamMask; mask != 0U; mask >>= 1U)
            armorDistinctTeams += static_cast<std::uint8_t>(mask & 1U);
        std::uint32_t publishedBedCount = 0U;
        ::AcquireSRWLockShared(&m_bedCacheLock);
        publishedBedCount = m_publishedBedCache.markerCount;
        ::ReleaseSRWLockShared(&m_bedCacheLock);
        const bool armorValid = localArmorTeam != bedwars::Team::Unknown &&
            armorDistinctTeams >= 2U && publishedBedCount > 0U;

        const bool matchEvidenceValid = sidebar.valid || rosterValid || armorValid;
        const bedwars::Team candidateTeam = sidebar.valid ? sidebar.ownTeam :
            rosterValid ? rosterOwnTeam : localArmorTeam;
        if (matchEvidenceValid) {
            m_sidebarMissingCount = 0U;
            if (candidateTeam == m_sidebarCandidateTeam) {
                if (m_sidebarStableCount < UINT8_MAX) ++m_sidebarStableCount;
            } else {
                m_sidebarCandidateTeam = candidateTeam;
                m_sidebarStableCount = 1U;
            }
            // A valid Sidebar "YOU" row is server-authored and can activate
            // immediately. Roster/armor fallbacks still require two samples.
            const std::uint8_t requiredStableSamples = sidebar.valid ? 1U : 2U;
            if (m_sidebarStableCount >= requiredStableSamples &&
                !m_snapshot.matchActive) {
                m_snapshot.matchActive = true;
                m_snapshot.ownTeam = bedwars::formatCode(candidateTeam);
            }
        } else {
            if (m_snapshot.matchActive) {
                // Entity tracking, TAB refreshes and Sidebar packet changes are
                // all transient. Once a match has been confirmed, only a
                // WorldClient lifecycle boundary may clear it; otherwise a far
                // enemy or a respawning teammate would flip the entire session
                // back into lobby mode.
                if (m_sidebarMissingCount < UINT8_MAX) ++m_sidebarMissingCount;
            } else {
                m_sidebarCandidateTeam = bedwars::Team::Unknown;
                m_sidebarStableCount = 0U;
            }
        }

        MatchProbeState nextProbe{};
        nextProbe.sidebarAvailable = sidebarAvailable;
        nextProbe.tabAvailable = cache->getNetHandler != nullptr &&
            cache->getPlayerInfoMap != nullptr && cache->getGameProfile != nullptr &&
            cache->gameProfileGetName != nullptr;
        nextProbe.sidebarEvidence = sidebar.valid;
        nextProbe.rosterEvidence = rosterValid;
        nextProbe.armorEvidence = armorValid;
        nextProbe.sidebarLines = static_cast<std::uint8_t>(
            std::min<std::size_t>(sidebarLineCount, UINT8_MAX));
        nextProbe.sidebarTeams = sidebar.distinctTeams;
        nextProbe.sidebarYouRows = sidebar.youRows;
        nextProbe.rosterTaggedPlayers = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(taggedPlayers, UINT8_MAX));
        nextProbe.rosterPlayers = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(nextCount, UINT8_MAX));
        nextProbe.rosterTeams = rosterDistinctTeams;
        nextProbe.rosterOwnTeam = bedwars::formatCode(rosterOwnTeam);
        nextProbe.localArmorTeam = bedwars::formatCode(localArmorTeam);
        nextProbe.armorTeams = armorDistinctTeams;
        nextProbe.stableCount = m_sidebarStableCount;
        if (!(nextProbe == m_matchProbe)) {
            m_matchProbe = nextProbe;
            ++m_matchProbeGeneration;
        }

        const bool nextMatchActive = m_snapshot.matchActive;
        const char nextOwnTeam = nextMatchActive ? m_snapshot.ownTeam : 'u';
        if (nextMatchActive && !previousMatchActive) {
            m_matchAnchorValid = true;
            m_matchAnchorX = positionX;
            m_matchAnchorY = positionY;
            m_matchAnchorZ = positionZ;
            m_lockedOwnBedKnown = false;
            m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
            ++m_bedOwnershipGeneration;
        } else if (!nextMatchActive && previousMatchActive) {
            m_matchAnchorValid = false;
            m_lockedOwnBedKnown = false;
            m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        }
        if (nextMatchActive) {
            std::uint32_t compactCount = 0U;
            for (std::uint32_t index = 0U; index < nextCount; ++index) {
                const bedwars::Team team = bedwars::parseRosterTeam(rosterFormatted[index]);
                if (team == bedwars::Team::Unknown) continue;
                nextPlayers[index].teamColor = bedwars::formatCode(team);
                if (compactCount != index) nextPlayers[compactCount] = nextPlayers[index];
                ++compactCount;
            }
            nextCount = compactCount;

            // A confirmed world's roster is monotonic: TAB can temporarily
            // omit an entry during respawn/network refresh, but that must not
            // turn a known teammate into an enemy or trigger duplicate API
            // queries. New, colour-tagged players are appended; uncoloured
            // startup NPCs/bots are never admitted.
            for (std::uint32_t oldIndex = 0U;
                 oldIndex < m_snapshot.playerCount &&
                 nextCount < nextPlayers.size(); ++oldIndex) {
                const PlayerIdentity& oldPlayer = m_snapshot.players[oldIndex];
                bool alreadyPresent = false;
                for (std::uint32_t index = 0U; index < nextCount; ++index) {
                    const bool sameUuid = nextPlayers[index].uuid[0U] != '\0' &&
                        oldPlayer.uuid[0U] != '\0' &&
                        std::strcmp(nextPlayers[index].uuid.data(),
                                    oldPlayer.uuid.data()) == 0;
                    if (sameUuid || std::strcmp(nextPlayers[index].name.data(),
                                                oldPlayer.name.data()) == 0) {
                        // Preserve the first confirmed team for this world.
                        nextPlayers[index].teamColor = oldPlayer.teamColor;
                        alreadyPresent = true;
                        break;
                    }
                }
                if (!alreadyPresent) nextPlayers[nextCount++] = oldPlayer;
            }
        } else {
            // Outside a confirmed match we deliberately publish no team
            // decisions, preventing lobby ranks/NPCs from reaching Debug chat
            // or the automatic Hypixel query pipeline.
            nextPlayers = {};
            nextCount = 0U;
        }

        std::sort(nextPlayers.begin(), nextPlayers.begin() + nextCount,
                  [](const PlayerIdentity& first, const PlayerIdentity& second) noexcept {
                      return std::strcmp(first.name.data(), second.name.data()) < 0;
                  });
        bool rosterChanged = nextCount != m_snapshot.playerCount;
        for (std::uint32_t index = 0U; !rosterChanged && index < nextCount; ++index) {
            rosterChanged = std::strcmp(nextPlayers[index].name.data(),
                                        m_snapshot.players[index].name.data()) != 0 ||
                            std::strcmp(nextPlayers[index].uuid.data(),
                                        m_snapshot.players[index].uuid.data()) != 0 ||
                            nextPlayers[index].teamColor != m_snapshot.players[index].teamColor;
        }

        const bool statusChanged = nextMatchActive != previousMatchActive ||
            nextOwnTeam != previousOwnTeam ||
            std::strcmp(nextLocalName.data(), m_snapshot.localPlayerName.data()) != 0;
        if (rosterChanged || statusChanged) {
            m_snapshot.players = nextPlayers;
            m_snapshot.playerCount = nextCount;
            m_snapshot.localPlayerName = nextLocalName;
            m_snapshot.matchActive = nextMatchActive;
            m_snapshot.ownTeam = nextOwnTeam;
            m_snapshot.playerRosterGeneration = ++m_playerRosterGeneration;
        }
    }

    // The scanner thread publishes immutable fixed storage. SwapBuffers only
    // takes a short shared SRW lock and performs no block JNI calls.
    if (singlePlayer == JNI_TRUE) {
        const bool previousOwnBedKnown = m_snapshot.ownBedKnown;
        const int previousOwnBedX = m_snapshot.ownBedX;
        const int previousOwnBedY = m_snapshot.ownBedY;
        const int previousOwnBedZ = m_snapshot.ownBedZ;
        const GameSnapshot::OwnBedSource previousOwnBedSource =
            m_snapshot.ownBedSource;
        ::AcquireSRWLockShared(&m_bedCacheLock);
        m_snapshot.bedMarkerCount = m_publishedBedCache.markerCount;
        m_snapshot.bedCount = m_publishedBedCache.markerCount;
        std::copy_n(m_publishedBedCache.markers.begin(),
                    m_publishedBedCache.markerCount, m_snapshot.bedMarkers.begin());
        m_snapshot.bedScanProgress = m_publishedBedCache.generation == 0U ? 0.0F : 1.0F;
        ::ReleaseSRWLockShared(&m_bedCacheLock);

        m_snapshot.ownBedKnown = false;
        m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
        if (m_snapshot.matchActive && m_snapshot.ownTeam != 'u') {
            const BedMarker* teamCandidate = nullptr;
            std::uint32_t matchingBeds = 0U;
            for (std::uint32_t index = 0U; index < m_snapshot.bedMarkerCount; ++index) {
                const BedMarker& bed = m_snapshot.bedMarkers[index];
                if (bed.teamColor != m_snapshot.ownTeam) continue;
                teamCandidate = &bed;
                ++matchingBeds;
            }

            // Team-coloured wool is authoritative whenever exactly one bed
            // matches. If a bed is initially unprotected, lock the nearest
            // bed to the stable match-start position and upgrade that lock
            // later when team-wool evidence arrives.
            if (matchingBeds == 1U && teamCandidate != nullptr &&
                (!m_lockedOwnBedKnown ||
                 m_lockedOwnBedSource == GameSnapshot::OwnBedSource::MatchSpawn)) {
                m_lockedOwnBedKnown = true;
                m_lockedOwnBedX = teamCandidate->x;
                m_lockedOwnBedY = teamCandidate->y;
                m_lockedOwnBedZ = teamCandidate->z;
                m_lockedOwnBedSource = GameSnapshot::OwnBedSource::TeamWool;
            }
            if (!m_lockedOwnBedKnown && m_matchAnchorValid) {
                const BedMarker* nearest = nullptr;
                double nearestDistanceSq = 36.0 * 36.0;
                for (std::uint32_t index = 0U; index < m_snapshot.bedMarkerCount; ++index) {
                    const BedMarker& bed = m_snapshot.bedMarkers[index];
                    const double centerX =
                        (static_cast<double>(bed.x + bed.footX) + 1.0) * 0.5;
                    const double centerZ =
                        (static_cast<double>(bed.z + bed.footZ) + 1.0) * 0.5;
                    const double dx = centerX - m_matchAnchorX;
                    const double dy = static_cast<double>(bed.y) - m_matchAnchorY;
                    const double dz = centerZ - m_matchAnchorZ;
                    if (std::abs(dy) > 16.0) continue;
                    const double distanceSq = dx * dx + dy * dy + dz * dz;
                    if (distanceSq < nearestDistanceSq) {
                        nearestDistanceSq = distanceSq;
                        nearest = &bed;
                    }
                }
                if (nearest != nullptr) {
                    m_lockedOwnBedKnown = true;
                    m_lockedOwnBedX = nearest->x;
                    m_lockedOwnBedY = nearest->y;
                    m_lockedOwnBedZ = nearest->z;
                    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::MatchSpawn;
                }
            }

            if (m_lockedOwnBedKnown) {
                bool lockedMarkerLoaded = false;
                for (std::uint32_t index = 0U; index < m_snapshot.bedMarkerCount; ++index) {
                    const BedMarker& bed = m_snapshot.bedMarkers[index];
                    if (bed.x != m_lockedOwnBedX || bed.y != m_lockedOwnBedY ||
                        bed.z != m_lockedOwnBedZ) continue;
                    lockedMarkerLoaded = true;
                    break;
                }
                // Bed caches are intentionally evicted when their chunk
                // unloads. Keep the ownership lock while the local player is
                // far away, but treat a missing marker in nearby/loaded range
                // as a real bed destruction so alerts disappear promptly.
                const double bedDx = (static_cast<double>(m_lockedOwnBedX) + 0.5) - positionX;
                const double bedDz = (static_cast<double>(m_lockedOwnBedZ) + 0.5) - positionZ;
                const bool bedShouldBeLoaded = bedDx * bedDx + bedDz * bedDz <= 96.0 * 96.0;
                if (!lockedMarkerLoaded && bedShouldBeLoaded) {
                    m_lockedOwnBedKnown = false;
                    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
                } else {
                    m_snapshot.ownBedKnown = true;
                    m_snapshot.ownBedX = m_lockedOwnBedX;
                    m_snapshot.ownBedY = m_lockedOwnBedY;
                    m_snapshot.ownBedZ = m_lockedOwnBedZ;
                    m_snapshot.ownBedSource = m_lockedOwnBedSource;
                }
            }
        } else {
            m_lockedOwnBedKnown = false;
            m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        }
        if (previousOwnBedKnown != m_snapshot.ownBedKnown ||
            previousOwnBedX != m_snapshot.ownBedX ||
            previousOwnBedY != m_snapshot.ownBedY ||
            previousOwnBedZ != m_snapshot.ownBedZ ||
            previousOwnBedSource != m_snapshot.ownBedSource) {
            ++m_bedOwnershipGeneration;
        }
    } else {
        m_snapshot.bedCount = 0U;
        m_snapshot.bedMarkerCount = 0U;
        m_snapshot.bedScanProgress = 0.0F;
        m_snapshot.ownBedKnown = false;
        m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
    }
    if (textureManagerObject != nullptr) env->DeleteLocalRef(textureManagerObject);

    env->PopLocalFrame(nullptr);
    m_snapshot.health = health;
    m_snapshot.maxHealth = maxHealth;
    m_snapshot.entityId = entityId;
    m_snapshot.x = positionX;
    m_snapshot.y = positionY;
    m_snapshot.z = positionZ;
    m_snapshot.bounds = box;
    m_snapshot.loadedEntities = loadedEntityCount;
    m_snapshot.mappingAttempt = m_mappingAttempt.load(std::memory_order_acquire);
    m_snapshot.mappingRetryInMs = 0U;
    m_snapshot.mapping = cache->profile->label.c_str();
    m_snapshot.state = GameSnapshot::State::Ready;
    return m_snapshot;
}


} // namespace mcoverlay
