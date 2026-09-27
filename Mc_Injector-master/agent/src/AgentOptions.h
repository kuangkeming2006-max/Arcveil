#pragma once

#include <string>

namespace mcoverlay {

struct AgentOptions final {
    bool enabled = false;
    bool interactive = false;
    std::wstring pipeName;
    std::string token;
    unsigned protocol = 1U;
    std::wstring mappingPack;
    std::string mappingHash;
    bool mappingOptionsValid = true;

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] AgentOptions parseAgentOptions(const char* options);

} // namespace mcoverlay
