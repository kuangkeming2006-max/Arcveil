#pragma once

#include "GameVersion.h"
#include <string>

namespace mcoverlay::bindings {

enum class GameApiShape : std::uint8_t { Legacy18 };
enum class VersionCapability : std::uint8_t { EntitySnapshots = 1, InventoryAccess = 2, BedScanning = 4 };
struct VersionCapabilities final {
    std::uint8_t bits = 0;
    [[nodiscard]] constexpr bool supports(VersionCapability capability) const noexcept {
        return (bits & static_cast<std::uint8_t>(capability)) != 0;
    }
};

// One immutable policy per API structure. Names/descriptors of mapped types
// still come from MappingDictionary; family/namespace never choose an adapter.
struct VersionAdapter final {
    std::string_view id;
    GameApiShape shape;
    VersionCapabilities capabilities;

    [[nodiscard]] std::string inventoryDescriptor(std::string_view itemStackSignature) const {
        // Legacy18 uses ItemStack[]. Do not advertise a List-based version
        // until both its lookup contract and access operations are implemented.
        switch (shape) {
        case GameApiShape::Legacy18: return std::string("[") + std::string(itemStackSignature);
        }
        return {}; // A new shape must implement its descriptor as well as access.
    }
};

inline constexpr VersionAdapter Legacy18Adapter{"legacy-1.8-api-v1", GameApiShape::Legacy18, {7}};

[[nodiscard]] inline constexpr const VersionAdapter* selectVersionAdapter(MinecraftVersion version) noexcept {
    // Exact allowlist: sharing names or a client family proves no API compatibility.
    // Additional verified versions may reuse this object when their API agrees.
    if (version == MinecraftVersion{1, 8, 9}) {
        return &Legacy18Adapter;
    }
    return nullptr;
}

} // namespace mcoverlay::bindings
