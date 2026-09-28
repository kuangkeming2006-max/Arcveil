#pragma once
#include <cstdint>
#include <cstddef>
namespace mcoverlay::mapping {
// McOverlay_Start returns tagged Win32 failures when the target cannot publish
// a status/error file. The loader preserves this value in its stderr diagnostic.
inline constexpr std::uint32_t probeRequestReadError = 0x40020000U;
inline constexpr std::size_t probeRequestBytes = 32768;
inline constexpr std::uint32_t probeOutputOpenError = 0x40000000U;
inline constexpr std::uint32_t probeOutputWriteError = 0x40010000U;
} // namespace mcoverlay::mapping
