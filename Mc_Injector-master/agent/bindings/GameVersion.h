#pragma once

#include <cstdint>
#include <string_view>

namespace mcoverlay::bindings {

// Pack-declared target identity, not a claim that a family/launch hint detected
// the runtime version. Live member/loader validation is still mandatory.
struct MinecraftVersion final {
    std::uint16_t major = 0, minor = 0, patch = 0;
    [[nodiscard]] constexpr bool known() const noexcept { return major != 0; }
    constexpr bool operator==(const MinecraftVersion&) const noexcept = default;

    [[nodiscard]] static constexpr MinecraftVersion parse(std::string_view text) noexcept {
        MinecraftVersion result;
        std::uint16_t* parts[] = {&result.major, &result.minor, &result.patch};
        unsigned count = 0;
        while (!text.empty() && count < 3) {
            const auto end = text.find('.');
            const auto part = text.substr(0, end);
            if (part.empty() || (part.size() > 1 && part.front() == '0')) return {};
            unsigned value = 0;
            for (const char c : part) {
                if (c < '0' || c > '9') return {};
                value = value * 10 + static_cast<unsigned>(c - '0');
                if (value > 65535) return {};
            }
            *parts[count++] = static_cast<std::uint16_t>(value);
            if (end == std::string_view::npos) return count >= 2 && result.known() ? result : MinecraftVersion{};
            text.remove_prefix(end + 1);
        }
        return {}; // Extra components, trailing dot or missing minor version.
    }
};

enum class MappingNamespace : std::uint8_t { Unknown, Notch, SRG, MCP, Custom };
[[nodiscard]] constexpr std::string_view mappingNamespaceName(MappingNamespace value) noexcept {
    switch (value) {
    case MappingNamespace::Notch: return "Notch";
    case MappingNamespace::SRG: return "SRG";
    case MappingNamespace::MCP: return "MCP";
    case MappingNamespace::Custom: return "Custom";
    default: return "Unknown";
    }
}

} // namespace mcoverlay::bindings
