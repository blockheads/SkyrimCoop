#pragma once

#include <cstdint>
#include <vector>

// Who adds this game's player's ragdoll to the physics world: each call of the add visitor (32953, graph vfunc 2) that
// put one of its graphs' ragdoll in (out before, in after), by the caller's address (an RVA in SkyrimSE.exe).
namespace TestHarness::RagdollWatch
{
struct Add
{
    uint64_t Tick; // GetTickCount64
    uintptr_t CallerRva;
};
[[nodiscard]] std::vector<Add> Adds(bool aClear) noexcept;
// Each ragdoll pre-physics pass (63562) that put the player's graph's ragdoll back in the world (hkpWorld_AddEntity
// 61304, past the visitor): out before the pass, in after. CallerRva 0.
[[nodiscard]] std::vector<Add> PrePhysicsReadds(bool aClear) noexcept;
// The player's ragdoll out of the world now, from the harness's poll in our update: at once on every graph (63622, as
// Revive did before 2026-11-22), or queued on the task pool (QueueRagdollOutOfWorld). False without a player.
bool TakeOut(bool aQueued) noexcept;
} // namespace TestHarness::RagdollWatch
