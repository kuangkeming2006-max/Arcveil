#pragma once

#include <string_view>

namespace mcoverlay::protocol {

// The controller owns the named-pipe server and the agent connects as a
// client. The protocol is deliberately line-oriented so Qt can implement it
// without sharing an ABI-sensitive C structure. Every message ends in '\n'.
//
// Agent -> controller:
//   HELLO 1 <pid> <32-hex-token>\n
//   HOOK_READY OpenGL\n
//   RENDERER_READY OpenGL\n
//   STATE_APPLIED <visible:0|1> <interactive:0|1>\n
//   STATE_CHANGED <visible:0|1> <interactive:0|1>\n
//   AIM_TARGET_FILTER_CHANGED <filter:0|1>\n
//   AIM_TARGET_FILTER_APPLIED <filter:0|1>\n
//   GUI_TYPOGRAPHY_CHANGED <size:14..24> <weight:400|600|700>\n
//   GUI_TYPOGRAPHY_APPLIED <size:14..24> <weight:400|600|700>\n
//   GUI_ELEMENT_SCALE_CHANGED <percent:60..150>\n
//   GUI_ELEMENT_SCALE_APPLIED <percent:60..150>\n
//   SCAFFOLD_CONFIG_APPLIED <enabled:0|1> <same-layer:0|1>\n
//   SMART_HOTBAR_APPLIED <validated-config-token>\n
//   GAMEPLAY_DIAGNOSTIC 1 <seq> <tick-ms> <configured-flags> <input-flags>
//     <binding-mask> <missing-bindings> <hook-mask> <update-calls> <hotbar-callbacks> <hotbar-actions>
//     <scaffold-attempts> <scaffold-accepted> <last-block-reason>\n
// Optional diagnostic receipts never authorize Ready or override configuration.
//   DETACH_COMPLETE\n
//   STATUS <single-line-text>\n
//   ERROR <code> <single-line-text>\n
//
// Controller -> agent:
//   STATE <visible:0|1> <interactive:0|1>\n
//   AIM_TARGET_FILTER <filter:0|1>\n
//   GUI_TYPOGRAPHY <size:14..24> <weight:400|600|700>\n
//   GUI_ELEMENT_SCALE <percent:60..150>\n
//   DETACH\n
//
// A broken pipe hides the overlay and leaves the signed/normal JVM agent
// loaded. It never attempts FreeLibrary from inside the target process.
inline constexpr unsigned kProtocolVersion = 1U;
inline constexpr std::string_view kStateCommand = "STATE";
inline constexpr std::string_view kDetachCommand = "DETACH";
inline constexpr std::size_t kMaximumLineBytes = 1024;

// Aim target filtering is independent of every FEATURE_STATE positional schema.
// 0 = Normal (default, including old configs); 1 = Force. Exactly one literal
// 0/1 token is accepted; unknown values/extra fields produce BAD_AIM_TARGET_FILTER.

} // namespace mcoverlay::protocol

// Smart Hotbar v3 (Controller and Agent must be upgraded together):
// SMART_HOTBAR / SMART_HOTBAR_CHANGED / SMART_HOTBAR_APPLIED carry one token:
// v3:<flags>:<key1>...:<key9>. Each key is an unsigned decimal uint32 with
// low-to-high nibbles in priority order, unique category IDs 1..8, zero=vanilla.
// flags preserve enabled/refill/sprint/all-names bits. v1/v2 numeric input migrates.
