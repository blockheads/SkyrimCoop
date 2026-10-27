#pragma once

#include <cstdint>

// How far behind its owner's time a copy is shown: its movement and its ragdoll alike, so the two agree
// (docs/design/physics-sync.md, "A copy's ragdoll, steered").
inline constexpr uint64_t kCopyDelayMs = 300;
