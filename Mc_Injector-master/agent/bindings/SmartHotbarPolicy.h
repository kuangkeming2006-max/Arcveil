#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mcoverlay::hotbar {

enum class Action : std::uint8_t { None=0U, Sword=1U, Blocks=2U, Shears=3U, Pickaxe=4U, Axe=5U };
enum class ItemKind : std::uint8_t { Empty, Other, Sword, Blocks, Shears, Pickaxe, Axe };

inline constexpr std::size_t SlotCount=9U;
// v2: marker 31; persistent refill/sprint/global-name preferences 28..30;
// enabled bit 0; nine three-bit categories 1..27. v1 remains readable.
inline constexpr std::uint32_t Version2=0x80000000U;
inline constexpr std::uint32_t Refill=0x10000000U;
inline constexpr std::uint32_t Sprint=0x20000000U;
inline constexpr std::uint32_t AllNames=0x40000000U;
inline constexpr std::uint32_t PackedMask=0xFFFFFFFFU;

[[nodiscard]] constexpr bool validAction(const int action) noexcept {
    return action>=static_cast<int>(Action::None)&&
        action<=static_cast<int>(Action::Axe);
}

[[nodiscard]] constexpr std::uint32_t pack(
    const bool enabled,const std::array<int,SlotCount>& actions,
    const bool refill=false,const bool sprint=false,const bool allNames=false) noexcept {
    std::uint32_t packed=Version2|(enabled?1U:0U)|(refill?Refill:0U)|
        (sprint?Sprint:0U)|(allNames?AllNames:0U);
    for(std::size_t slot=0;slot<actions.size();++slot) {
        const auto action=static_cast<std::uint32_t>(
            std::clamp(actions[slot],0,5));
        packed|=action<<(1U+static_cast<unsigned>(slot)*3U);
    }
    return packed;
}

[[nodiscard]] constexpr bool validPacked(const std::uint32_t packed) noexcept {
    const bool v2=(packed&Version2)!=0U;
    if(!v2&&(packed&~((1U<<19U)-1U))!=0U) return false;
    for(std::size_t slot=0;slot<SlotCount;++slot)
        if(((packed>>(1U+static_cast<unsigned>(slot)*(v2?3U:2U)))&(v2?7U:3U))>(v2?5U:2U))
            return false;
    return true;
}

[[nodiscard]] constexpr bool enabled(const std::uint32_t packed) noexcept {
    return (packed&1U)!=0U;
}

[[nodiscard]] constexpr std::array<int,SlotCount> unpack(
    const std::uint32_t packed) noexcept {
    std::array<int,SlotCount> actions{};
    const bool v2=(packed&Version2)!=0U;
    for(std::size_t slot=0;slot<actions.size();++slot)
        actions[slot]=static_cast<int>(
            (packed>>(1U+static_cast<unsigned>(slot)*(v2?3U:2U)))&(v2?7U:3U));
    return actions;
}

[[nodiscard]] constexpr bool matches(const ItemKind kind,
                                     const int action) noexcept {
    return (action==static_cast<int>(Action::Sword)&&kind==ItemKind::Sword)||
        (action==static_cast<int>(Action::Blocks)&&kind==ItemKind::Blocks)||
        (action==static_cast<int>(Action::Shears)&&kind==ItemKind::Shears)||
        (action==static_cast<int>(Action::Pickaxe)&&kind==ItemKind::Pickaxe)||
        (action==static_cast<int>(Action::Axe)&&kind==ItemKind::Axe);
}

[[nodiscard]] constexpr ItemKind toolKind(const int id) noexcept {
    switch(id) {
    case 359:return ItemKind::Shears;
    case 257:case 270:case 274:case 278:case 285:return ItemKind::Pickaxe;
    case 258:case 271:case 275:case 279:case 286:return ItemKind::Axe;
    default:return ItemKind::Other;
    }
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

// Switching to a ready hotbar stack is safer than an inventory click and is
// therefore always the first refill choice.
[[nodiscard]] inline int selectRefillSource(
    const std::span<const ItemKind> inventory,const int currentSlot) noexcept {
    return selectSource(inventory,currentSlot,static_cast<int>(Action::Blocks));
}

} // namespace mcoverlay::hotbar
