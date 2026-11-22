#pragma once

#include <cstdint>
#include <vector>

// Who adds this game's player's ragdoll to the physics world: each call of the add visitor (32953, graph vfunc 2) on
// one of its graphs, by the caller's address (an RVA in SkyrimSE.exe).
namespace TestHarness::RagdollWatch
{
struct Add
{
    uint64_t Tick; // GetTickCount64
    uintptr_t CallerRva;
};
[[nodiscard]] std::vector<Add> Adds(bool aClear) noexcept;
// Each time the ragdoll pre-physics pass (63562) met the player's graph with its ragdoll out of the world: that pass then
// adds every body straight back (hkpWorld_AddEntity), past the visitor. CallerRva 0.
[[nodiscard]] std::vector<Add> PrePhysicsReadds(bool aClear) noexcept;
} // namespace TestHarness::RagdollWatch
