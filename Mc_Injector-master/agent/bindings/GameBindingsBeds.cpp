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

void GameBindings::runBedScanner(JNIEnv* const env, HANDLE const stopEvent) noexcept
{
    if (env == nullptr || stopEvent == nullptr) return;

    using ChunkBeds = std::unordered_map<std::uint64_t, std::vector<BedMarker>>;
    struct DefenseSample final {
        int x = 0;
        int y = 0;
        int z = 0;
        std::uint16_t blockId = 0U;
        std::uint8_t metadata = 0U;
    };
    using ChunkDefense = std::unordered_map<std::uint64_t, std::vector<DefenseSample>>;
    ChunkBeds bedsByChunk;
    ChunkDefense defenseByChunk;
    std::unordered_set<std::uint64_t> processedChunks;
    std::unordered_set<std::uint64_t> currentChunks;
    jobject worldIdentity = nullptr;
    std::uint64_t generation = 0U;
    std::uint64_t lastVerification = 0U;
    std::uint64_t lastDefenseRefresh = 0U;
    const HANDLE waitHandles[2]{stopEvent, m_bedRescanEvent};
    const DWORD waitHandleCount = m_bedRescanEvent != nullptr ? 2U : 1U;
    const auto stopRequested = [&](const DWORD timeout) noexcept {
        const DWORD wait = ::WaitForMultipleObjects(
            waitHandleCount, waitHandles, FALSE, timeout);
        return wait == WAIT_OBJECT_0 || wait == WAIT_FAILED;
    };

    const auto chunkKey = [](const jint x, const jint z) noexcept {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
               static_cast<std::uint32_t>(z);
    };
    const auto publish = [&]() noexcept {
        PublishedBedCache next{};
        next.loadedChunkCount = static_cast<std::uint32_t>(
            std::min<std::size_t>(processedChunks.size(), UINT32_MAX));
        next.generation = ++generation;
        for (const auto& [key, markers] : bedsByChunk) {
            (void)key;
            for (const BedMarker& marker : markers) {
                if (next.markerCount >= next.markers.size()) break;
                BedMarker enriched = marker;
                std::array<unsigned, 8U> teamWoolEvidence{};
                // The bulk-copied sparse material cache is folded into exact
                // distance rings here on the scanner thread. SwapBuffers only
                // sums at most ten small counters when drawing the panel.
                for (const auto& [defenseKey, samples] : defenseByChunk) {
                    const auto chunkX = static_cast<std::int32_t>(defenseKey >> 32U);
                    const auto chunkZ = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(defenseKey));
                    const auto floorChunk = [](const int coordinate) noexcept {
                        return coordinate >= 0 ? coordinate / 16 : (coordinate - 15) / 16;
                    };
                    const int headChunkX = floorChunk(marker.x);
                    const int headChunkZ = floorChunk(marker.z);
                    const int footChunkX = floorChunk(marker.footX);
                    const int footChunkZ = floorChunk(marker.footZ);
                    const bool nearHead = std::abs(chunkX - headChunkX) <= 1 &&
                                          std::abs(chunkZ - headChunkZ) <= 1;
                    const bool nearFoot = std::abs(chunkX - footChunkX) <= 1 &&
                                          std::abs(chunkZ - footChunkZ) <= 1;
                    if (!nearHead && !nearFoot) continue;

                    for (const DefenseSample& sample : samples) {
                        const int vertical = sample.y - marker.y;
                        if (vertical < 0 || vertical > 10) continue;
                        const int dx = std::min(std::abs(sample.x - marker.x),
                                                std::abs(sample.x - marker.footX));
                        const int dz = std::min(std::abs(sample.z - marker.z),
                                                std::abs(sample.z - marker.footZ));
                        const int ring = std::max({dx, dz, vertical});
                        if (ring < 1 || ring > 10) continue;

                        if (sample.blockId == 35U && ring <= 6) {
                            const bedwars::Team woolTeam =
                                bedwars::fromWoolMetadata(sample.metadata);
                            const std::uint8_t woolIndex = bedwars::teamIndex(woolTeam);
                            if (woolIndex < teamWoolEvidence.size()) {
                                // Nearby wool is stronger evidence than map
                                // decoration near the edge of the scan radius.
                                teamWoolEvidence[woolIndex] +=
                                    static_cast<unsigned>(7 - ring);
                            }
                        }

                        std::uint8_t normalizedMeta = sample.metadata;
                        if (sample.blockId == 17U || sample.blockId == 162U) {
                            normalizedMeta &= 0x3U;
                        }

                        BedDefenseBlock* summary = nullptr;
                        for (std::size_t index = 0U; index < enriched.defenseCount; ++index) {
                            BedDefenseBlock& candidate = enriched.defense[index];
                            if (candidate.blockId == sample.blockId &&
                                candidate.metadata == normalizedMeta) {
                                summary = &candidate;
                                break;
                            }
                        }
                        if (summary == nullptr &&
                            enriched.defenseCount < enriched.defense.size()) {
                            summary = &enriched.defense[enriched.defenseCount++];
                            summary->blockId = sample.blockId;
                            summary->metadata = normalizedMeta;
                        }
                        if (summary != nullptr) {
                            std::uint16_t& count = summary->ringCounts[
                                static_cast<std::size_t>(ring)];
                            if (count != std::numeric_limits<std::uint16_t>::max()) ++count;
                        }
                    }
                }
                unsigned bestEvidence = 0U;
                unsigned secondEvidence = 0U;
                std::uint8_t bestTeam = 0xFFU;
                for (std::uint8_t index = 0U; index < teamWoolEvidence.size(); ++index) {
                    const unsigned evidence = teamWoolEvidence[index];
                    if (evidence > bestEvidence) {
                        secondEvidence = bestEvidence;
                        bestEvidence = evidence;
                        bestTeam = index;
                    } else if (evidence > secondEvidence) {
                        secondEvidence = evidence;
                    }
                }
                // Fail closed on weak or mixed-colour evidence. This removes
                // the previous "nearest bed" false ownership assignment.
                if (bestTeam < 8U && bestEvidence >= 4U &&
                    bestEvidence >= secondEvidence + 2U) {
                    enriched.teamColor = bedwars::formatCode(
                        static_cast<bedwars::Team>(bestTeam + 1U));
                }
                next.markers[next.markerCount++] = enriched;
            }
            if (next.markerCount >= next.markers.size()) break;
        }
        ::AcquireSRWLockExclusive(&m_bedCacheLock);
        m_publishedBedCache = next;
        ::ReleaseSRWLockExclusive(&m_bedCacheLock);
    };
    const auto clearWorld = [&]() noexcept {
        processedChunks.clear();
        currentChunks.clear();
        bedsByChunk.clear();
        defenseByChunk.clear();
        publish();
    };

    while (::WaitForSingleObject(stopEvent, 0U) == WAIT_TIMEOUT) {
        if (m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved ||
            m_cache == nullptr) {
            if (stopRequested(100U)) break;
            continue;
        }
        BindingCache* const cache = m_cache.get();
        // Only this scanner thread owns processedChunks/bedsByChunk. The UI and
        // IPC threads publish one atomic bit, so a manual refresh cannot race
        // vector/hash-table mutation or issue JNI calls from the wrong thread.
        if (m_bedRescanRequested.exchange(false, std::memory_order_acq_rel)) {
            processedChunks.clear();
        }
        if (env->PushLocalFrame(96) < 0) {
            clearException(env);
            if (stopRequested(500U)) break;
            continue;
        }

        bool cycleValid = true;
        jobject minecraft = cache->minecraftInstanceField != nullptr
            ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
            : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
        if (env->ExceptionCheck() == JNI_TRUE || minecraft == nullptr) cycleValid = false;
        jboolean singlePlayer = JNI_FALSE;
        jobject world = nullptr;
        if (cycleValid) {
            singlePlayer = JNI_TRUE;
            if (env->ExceptionCheck() == JNI_TRUE) cycleValid = false;
        }
        if (cycleValid) {
            world = env->GetObjectField(minecraft, cache->worldField);
            if (env->ExceptionCheck() == JNI_TRUE) cycleValid = false;
        }

        if (!cycleValid || singlePlayer != JNI_TRUE || world == nullptr) {
            clearException(env);
            if (worldIdentity != nullptr) {
                env->DeleteGlobalRef(worldIdentity);
                worldIdentity = nullptr;
            }
            if (!processedChunks.empty() || !bedsByChunk.empty()) clearWorld();
            env->PopLocalFrame(nullptr);
            if (stopRequested(500U)) break;
            continue;
        }

        if (worldIdentity == nullptr || env->IsSameObject(worldIdentity, world) != JNI_TRUE) {
            if (worldIdentity != nullptr) env->DeleteGlobalRef(worldIdentity);
            worldIdentity = env->NewGlobalRef(world);
            clearWorld();
        }

        jobject provider = env->CallObjectMethod(world, cache->getChunkProvider);
        if (env->ExceptionCheck() == JNI_TRUE || provider == nullptr ||
            env->IsInstanceOf(provider, cache->chunkProviderClass) != JNI_TRUE) {
            clearException(env);
            env->PopLocalFrame(nullptr);
            if (stopRequested(500U)) break;
            continue;
        }
        jobject listing = env->GetObjectField(provider, cache->chunkListingField);
        jobjectArray chunks = listing == nullptr ? nullptr : static_cast<jobjectArray>(
            env->CallObjectMethod(listing, cache->listToArray));
        if (env->ExceptionCheck() == JNI_TRUE || chunks == nullptr) {
            clearException(env);
            env->PopLocalFrame(nullptr);
            if (stopRequested(500U)) break;
            continue;
        }

        currentChunks.clear();
        const jsize chunkCount = std::min<jsize>(env->GetArrayLength(chunks), 2048);
        for (jsize chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
            jobject chunk = env->GetObjectArrayElement(chunks, chunkIndex);
            if (env->ExceptionCheck() == JNI_TRUE) {
                clearException(env);
                continue;
            }
            if (chunk == nullptr || env->IsInstanceOf(chunk, cache->chunkClass) != JNI_TRUE) {
                if (chunk != nullptr) env->DeleteLocalRef(chunk);
                continue;
            }
            const jint chunkX = env->GetIntField(chunk, cache->chunkX);
            const jint chunkZ = env->GetIntField(chunk, cache->chunkZ);
            if (env->ExceptionCheck() == JNI_TRUE) {
                clearException(env);
                env->DeleteLocalRef(chunk);
                continue;
            }
            const std::uint64_t key = chunkKey(chunkX, chunkZ);
            currentChunks.insert(key);
            if (!processedChunks.insert(key).second) {
                env->DeleteLocalRef(chunk);
                continue;
            }

            std::vector<BedMarker> discovered;
            std::vector<DefenseSample> defenseSamples;
            jobjectArray sections = static_cast<jobjectArray>(
                env->CallObjectMethod(chunk, cache->getStorageArrays));
            if (env->ExceptionCheck() == JNI_TRUE || sections == nullptr) {
                clearException(env);
                processedChunks.erase(key); // transient read: retry next cycle
                env->DeleteLocalRef(chunk);
                continue;
            }
            const jsize sectionCount = std::min<jsize>(env->GetArrayLength(sections), 16);
            std::array<jchar, 4096U> states{};
            bool sectionReadFailed = false;
            for (jsize sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
                jobject section = env->GetObjectArrayElement(sections, sectionIndex);
                if (env->ExceptionCheck() == JNI_TRUE) {
                    clearException(env);
                    sectionReadFailed = true;
                    continue;
                }
                if (section == nullptr) continue;
                jcharArray data = static_cast<jcharArray>(
                    env->CallObjectMethod(section, cache->getStorageData));
                if (env->ExceptionCheck() == JNI_TRUE || data == nullptr ||
                    env->GetArrayLength(data) != static_cast<jsize>(states.size())) {
                    clearException(env);
                    sectionReadFailed = true;
                    if (data != nullptr) env->DeleteLocalRef(data);
                    env->DeleteLocalRef(section);
                    continue;
                }
                // 1.8.9 stores the global block-state ID in char[4096]. A
                // single bulk copy replaces 4096 getBlockState JNI calls.
                env->GetCharArrayRegion(data, 0, static_cast<jsize>(states.size()), states.data());
                if (env->ExceptionCheck() == JNI_TRUE) {
                    clearException(env);
                    sectionReadFailed = true;
                    env->DeleteLocalRef(data);
                    env->DeleteLocalRef(section);
                    continue;
                }
                for (std::size_t index = 0U; index < states.size(); ++index) {
                    const unsigned encoded = static_cast<unsigned>(states[index]);
                    const unsigned blockId = encoded >> 4U;
                    // Bed Wars defenses use a small, stable material palette.
                    // Retaining only those sparse samples avoids a 128 KiB
                    // dense copy per loaded chunk while still showing the
                    // blocks players actually place around a bed.
                    const bool defenseMaterial = blockId == 1U || blockId == 4U ||
                        blockId == 5U || blockId == 17U || blockId == 20U ||
                        blockId == 24U || blockId == 35U || blockId == 45U ||
                        blockId == 49U || blockId == 95U || blockId == 121U ||
                        blockId == 159U;
                    if (defenseMaterial) {
                        const int localX = static_cast<int>(index & 15U);
                        const int localZ = static_cast<int>((index >> 4U) & 15U);
                        const int localY = static_cast<int>((index >> 8U) & 15U);
                        defenseSamples.push_back(DefenseSample{
                            chunkX * 16 + localX,
                            static_cast<int>(sectionIndex) * 16 + localY,
                            chunkZ * 16 + localZ,
                            static_cast<std::uint16_t>(blockId),
                            static_cast<std::uint8_t>(encoded & 0xFU)});
                    }
                    if ((encoded >> 4U) != 26U || (encoded & 0x8U) == 0U) continue;
                    const int localX = static_cast<int>(index & 15U);
                    const int localZ = static_cast<int>((index >> 4U) & 15U);
                    const int localY = static_cast<int>((index >> 8U) & 15U);
                    const int headX = chunkX * 16 + localX;
                    const int headZ = chunkZ * 16 + localZ;
                    int footX = headX;
                    int footZ = headZ;
                    // BlockBed stores horizontal facing in metadata bits 0-1.
                    // getHorizontal maps 0=SOUTH, 1=WEST, 2=NORTH, 3=EAST;
                    // the scanned HEAD is one block along that facing from
                    // the FOOT, so walk in the opposite direction here.
                    switch (encoded & 0x3U) {
                    case 0U: --footZ; break;
                    case 1U: ++footX; break;
                    case 2U: ++footZ; break;
                    case 3U: --footX; break;
                    default: break;
                    }
                    discovered.push_back(BedMarker{
                        headX,
                        static_cast<int>(sectionIndex) * 16 + localY,
                        headZ,
                        footX,
                        footZ});
                }
                env->DeleteLocalRef(data);
                env->DeleteLocalRef(section);
            }
            bedsByChunk[key] = std::move(discovered);
            defenseByChunk[key] = std::move(defenseSamples);
            if (sectionReadFailed) {
                // Concurrent chunk mutation can invalidate one local section
                // read. Publish any safe partial result now, but remove the
                // processed marker so the complete chunk is retried in 500 ms.
                processedChunks.erase(key);
            }
            env->DeleteLocalRef(sections);
            env->DeleteLocalRef(chunk);
        }

        for (auto it = processedChunks.begin(); it != processedChunks.end();) {
            if (!currentChunks.contains(*it)) {
                bedsByChunk.erase(*it);
                defenseByChunk.erase(*it);
                it = processedChunks.erase(it);
            } else {
                ++it;
            }
        }

        const std::uint64_t now = static_cast<std::uint64_t>(::GetTickCount64());
        if (now - lastVerification >= 100U) {
            lastVerification = now;
            for (auto& [key, markers] : bedsByChunk) {
                (void)key;
                for (auto marker = markers.begin(); marker != markers.end();) {
                    jobject position = env->NewObject(cache->blockPosClass,
                                                      cache->blockPosConstructor,
                                                      marker->x, marker->y, marker->z);
                    jobject state = position == nullptr ? nullptr :
                        env->CallObjectMethod(world, cache->getBlockState, position);
                    jobject block = state == nullptr ? nullptr :
                        env->CallObjectMethod(state, cache->getBlock);
                    const bool failed = env->ExceptionCheck() == JNI_TRUE;
                    if (failed) clearException(env);
                    const bool stillBed = !failed && block != nullptr &&
                        env->IsInstanceOf(block, cache->bedClass) == JNI_TRUE;
                    if (block != nullptr) env->DeleteLocalRef(block);
                    if (state != nullptr) env->DeleteLocalRef(state);
                    if (position != nullptr) env->DeleteLocalRef(position);
                    if (!failed && !stillBed) marker = markers.erase(marker);
                    else ++marker;
                }
            }
        }

        if (now - lastDefenseRefresh >= 750U) {
            lastDefenseRefresh = now;
            const auto floorChunk = [](const int coordinate) noexcept {
                return coordinate >= 0 ? coordinate / 16 : (coordinate - 15) / 16;
            };
            // Chunk diffing remains the discovery mechanism for arbitrary
            // world data. Only the bounded 3x3 neighbourhood around an
            // already-known bed is invalidated here, so placed/broken defense
            // blocks refresh automatically without rescanning every loaded
            // chunk or issuing per-block JNI calls.
            for (const auto& [bedChunk, markers] : bedsByChunk) {
                (void)bedChunk;
                for (const BedMarker& marker : markers) {
                    const int centerChunkX = floorChunk(marker.x);
                    const int centerChunkZ = floorChunk(marker.z);
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            processedChunks.erase(chunkKey(
                                static_cast<jint>(centerChunkX + dx),
                                static_cast<jint>(centerChunkZ + dz)));
                        }
                    }
                }
            }
        }

        publish();
        env->PopLocalFrame(nullptr);
        if (stopRequested(100U)) break;
    }

    if (worldIdentity != nullptr) env->DeleteGlobalRef(worldIdentity);
    clearException(env);
}

void GameBindings::requestBedRescan() noexcept
{
    m_bedRescanRequested.store(true, std::memory_order_release);
    if (m_bedRescanEvent != nullptr) {
        ::SetEvent(m_bedRescanEvent);
    }
}


} // namespace mcoverlay
