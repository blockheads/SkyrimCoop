#pragma once

#include <cstdint>

// The copy contract for the running package, with no engine (docs/re/systems/actor-ai-and-copies.md): what
// ActorPackage::InitFromPackage (38959) puts in a copy's running slot. Checked by ClientTests.
namespace CopyPackage
{
// The package to put on: a copy holds its owner's once known, whatever the engine picks (a follower alias's combat
// override); until then, and for this game's own actors, the engine's choice.
[[nodiscard]] constexpr uint32_t Put(bool aCopy, uint32_t aOwnerPackage, uint32_t aRequested) noexcept
{
    return aCopy && aOwnerPackage ? aOwnerPackage : aRequested;
}
} // namespace CopyPackage
