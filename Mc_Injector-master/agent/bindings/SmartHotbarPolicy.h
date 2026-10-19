#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <charconv>
#include <bit>
#include <mutex>
#include <atomic>

namespace mcoverlay::hotbar {

enum class Action : std::uint8_t { None=0U, Sword=1U, Blocks=2U, Shears=3U, Pickaxe=4U, Axe=5U, Pearl=6U, Bow=7U, Tnt=8U };
enum class ItemKind : std::uint8_t { Empty, Other, Sword, Blocks, Shears, Pickaxe, Axe, Pearl, Bow, Tnt };

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
        action<=static_cast<int>(Action::Tnt);
}

[[nodiscard]] constexpr std::uint32_t packLegacy(
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

// v3 stores an ordered, duplicate-free nibble list per key (highest priority first).
// A value with no categories preserves vanilla slot selection. Legacy values migrate.
struct Config {
    std::uint32_t flags=0;
    std::array<int,SlotCount> actions{};
    bool operator==(const Config&) const = default;
};
// Publish all nine lists together; readers never observe half of a UI edit.
class AtomicConfig {
    mutable std::mutex mutex;
    Config value{};
public:
    Config load(std::memory_order = std::memory_order_seq_cst) const noexcept {
        std::lock_guard lock(mutex); return value;
    }
    void store(Config next,std::memory_order = std::memory_order_seq_cst) noexcept {
        std::lock_guard lock(mutex); value=next;
    }
};
inline constexpr std::uint32_t operator&(const Config& c,std::uint32_t mask) noexcept {return c.flags&mask;}
inline constexpr bool enabled(const Config& c) noexcept {return (c.flags&1U)!=0;}
inline constexpr auto unpack(const Config& c) noexcept {return c.actions;}
inline constexpr bool validPriority(int value) noexcept {
    auto bits=std::bit_cast<std::uint32_t>(value);
    unsigned seen=0;
    while(bits) {
        const unsigned a=bits&15U;
        if(a<1||a>8||(seen&(1U<<a))) return false;
        seen|=1U<<a; bits>>=4;
    }
    return true;
}
inline constexpr Config pack(bool on,const std::array<int,SlotCount>& actions,
    bool refill=false,bool sprint=false,bool names=false) noexcept {
    Config c{(on?1U:0U)|(refill?Refill:0U)|(sprint?Sprint:0U)|(names?AllNames:0U),actions};
    for(auto& a:c.actions) if(!validPriority(a)) a=0;
    return c;
}
inline constexpr bool validPacked(const Config& c) noexcept {
    if(c.flags&~(1U|Refill|Sprint|AllNames)) return false;
    for(auto a:c.actions) if(!validPriority(a)) return false;
    return true;
}
inline std::string encode(const Config& c) {
    std::string s="v3:"+std::to_string(c.flags);
    for(auto a:c.actions) s+=":"+std::to_string(std::bit_cast<std::uint32_t>(a));
    return s;
}
inline bool decode(std::string_view s,Config& result) noexcept {
    auto number=[](std::string_view token,std::uint32_t& n) {
        auto [end,ec]=std::from_chars(token.data(),token.data()+token.size(),n);
        return ec==std::errc{}&&end==token.data()+token.size();
    };
    Config c{};
    if(!s.starts_with("v3:")) {
        std::uint32_t old=0;
        if(!number(s,old)||!validPacked(old)) return false;
        c=pack(enabled(old),unpack(old),(old&Refill)!=0,(old&Sprint)!=0,(old&AllNames)!=0);
    } else {
        s.remove_prefix(3);
        for(std::size_t i=0;i<=SlotCount;++i) {
            const auto sep=s.find(':');
            if((i<SlotCount&&sep==s.npos)||(i==SlotCount&&sep!=s.npos)) return false;
            std::uint32_t n=0;
            if(!number(s.substr(0,sep),n)) return false;
            if(i==0) c.flags=n; else c.actions[i-1]=std::bit_cast<int>(n);
            if(sep!=s.npos) s.remove_prefix(sep+1);
        }
    }
    if(!validPacked(c)) return false;
    result=c; return true;
}
inline constexpr bool pressurePlate(int id) noexcept {return id==70||id==72||id==147||id==148;}
[[nodiscard]] constexpr bool matches(const ItemKind kind,
                                     const int action) noexcept {
    return (action==static_cast<int>(Action::Sword)&&kind==ItemKind::Sword)||
        (action==static_cast<int>(Action::Blocks)&&kind==ItemKind::Blocks)||
        (action==static_cast<int>(Action::Shears)&&kind==ItemKind::Shears)||
        (action==static_cast<int>(Action::Pickaxe)&&kind==ItemKind::Pickaxe)||
        (action==static_cast<int>(Action::Axe)&&kind==ItemKind::Axe)||
        (action==6&&kind==ItemKind::Pearl)||(action==7&&kind==ItemKind::Bow)||
        (action==8&&kind==ItemKind::Tnt);
}

[[nodiscard]] constexpr ItemKind toolKind(const int id) noexcept {
    switch(id) {
    case 368:return ItemKind::Pearl;
    case 261:return ItemKind::Bow;
    case 46:return ItemKind::Tnt;
    case 359:return ItemKind::Shears;
    case 257:case 270:case 274:case 278:case 285:return ItemKind::Pickaxe;
    case 258:case 271:case 275:case 279:case 286:return ItemKind::Axe;
    default:return ItemKind::Other;
    }
}

inline constexpr ItemKind classify(int id,bool sword,bool block) noexcept {
    if(id<=0||pressurePlate(id)) return ItemKind::Other;
    if(id==46) return ItemKind::Tnt;
    if(sword) return ItemKind::Sword;
    if(block) return ItemKind::Blocks;
    return toolKind(id);
}

// Prefer the already-held matching item, then another hotbar item, then the
// main inventory. This avoids needless swaps and mirrors the low-friction
// expectation of a category shortcut.
[[nodiscard]] inline int selectSource(const std::span<const ItemKind> inventory,
                                      const int currentSlot,
                                      const int action) noexcept {
    if(!validPriority(action)||action==0||inventory.empty()) return -1;
    auto priorities=std::bit_cast<std::uint32_t>(action);
    while(priorities) {
    const int category=static_cast<int>(priorities&15U);
    priorities>>=4;
    if(currentSlot>=0&&currentSlot<static_cast<int>(inventory.size())&&
       matches(inventory[static_cast<std::size_t>(currentSlot)],category))
        return currentSlot;
    const std::size_t hotbarEnd=std::min(SlotCount,inventory.size());
    for(std::size_t slot=0;slot<hotbarEnd;++slot)
        if(matches(inventory[slot],category)) return static_cast<int>(slot);
    for(std::size_t slot=hotbarEnd;slot<inventory.size();++slot)
        if(matches(inventory[slot],category)) return static_cast<int>(slot);
    }
    return -1;
}

// Switching to a ready hotbar stack is safer than an inventory click and is
// therefore always the first refill choice.
[[nodiscard]] inline int selectRefillSource(
    const std::span<const ItemKind> inventory,const int currentSlot) noexcept {
    return selectSource(inventory,currentSlot,static_cast<int>(Action::Blocks));
}

} // namespace mcoverlay::hotbar
