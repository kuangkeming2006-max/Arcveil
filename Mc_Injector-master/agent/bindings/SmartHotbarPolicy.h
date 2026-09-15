#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mcoverlay::hotbar {

enum class Action : std::uint8_t { None=0U, Sword=1U, Blocks=2U };
enum class ItemKind : std::uint8_t { Empty, Other, Sword, Blocks };

inline constexpr std::size_t SlotCount=9U;
inline constexpr std::uint32_t PackedMask=(1U<<(1U+SlotCount*2U))-1U;

[[nodiscard]] constexpr bool validAction(const int action) noexcept {
    return action>=static_cast<int>(Action::None)&&
        action<=static_cast<int>(Action::Blocks);
}

[[nodiscard]] constexpr std::uint32_t pack(
    const bool enabled,const std::array<int,SlotCount>& actions) noexcept {
    std::uint32_t packed=enabled?1U:0U;
    for(std::size_t slot=0;slot<actions.size();++slot) {
        const auto action=static_cast<std::uint32_t>(
            std::clamp(actions[slot],0,2));
        packed|=action<<(1U+static_cast<unsigned>(slot)*2U);
    }
    return packed;
}

[[nodiscard]] constexpr bool validPacked(const std::uint32_t packed) noexcept {
    if((packed&~PackedMask)!=0U) return false;
    for(std::size_t slot=0;slot<SlotCount;++slot)
        if(((packed>>(1U+static_cast<unsigned>(slot)*2U))&3U)>2U)
            return false;
    return true;
}

[[nodiscard]] constexpr bool enabled(const std::uint32_t packed) noexcept {
    return (packed&1U)!=0U;
}

[[nodiscard]] constexpr std::array<int,SlotCount> unpack(
    const std::uint32_t packed) noexcept {
    std::array<int,SlotCount> actions{};
    for(std::size_t slot=0;slot<actions.size();++slot)
        actions[slot]=static_cast<int>(
            (packed>>(1U+static_cast<unsigned>(slot)*2U))&3U);
    return actions;
}

[[nodiscard]] constexpr bool matches(const ItemKind kind,
                                     const int action) noexcept {
    return (action==static_cast<int>(Action::Sword)&&kind==ItemKind::Sword)||
        (action==static_cast<int>(Action::Blocks)&&kind==ItemKind::Blocks);
}

// Prefer the already-held matching item, then another hotbar item, then the
// main inventory. This avoids needless swaps and mirrors the low-friction
// expectation of a category shortcut.
[[nodiscard]] inline int selectSource(const std::span<const ItemKind> inventory,
                                      const int currentSlot,
                                      const int action) noexcept {
    if(!validAction(action)||action==0||inventory.empty()) return -1;
    if(currentSlot>=0&&currentSlot<static_cast<int>(inventory.size())&&
       matches(inventory[static_cast<std::size_t>(currentSlot)],action))
        return currentSlot;
    const std::size_t hotbarEnd=std::min(SlotCount,inventory.size());
    for(std::size_t slot=0;slot<hotbarEnd;++slot)
        if(matches(inventory[slot],action)) return static_cast<int>(slot);
    for(std::size_t slot=hotbarEnd;slot<inventory.size();++slot)
        if(matches(inventory[slot],action)) return static_cast<int>(slot);
    return -1;
}

} // namespace mcoverlay::hotbar
