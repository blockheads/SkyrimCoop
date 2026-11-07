#pragma once

#include <cstdint>

// The copy contract for package storage and reads, without the engine (actor-ai-and-copies.md).
namespace CopyPackage
{
// The package to put on: a copy holds its owner's once known, whatever the engine picks (a follower alias's combat
// override); until then, and for this game's own actors, the engine's choice.
[[nodiscard]] constexpr uint32_t Put(bool aCopy, uint32_t aOwnerPackage, uint32_t aRequested) noexcept
{
    return aCopy && aOwnerPackage ? aOwnerPackage : aRequested;
}
// An explicit none must not fall back to an old package behind the copy's idle placeholder.
[[nodiscard]] constexpr uint32_t Read(bool aCopy, uint32_t aOwnerPackage, uint32_t aNativePackage) noexcept
{
    return aCopy && !aOwnerPackage ? 0 : aNativePackage;
}
// A do-nothing package for a copy, made as InitiateDoNothingPackage (37402) makes it: delete-when-done, and its procedure
// type filled (29582). CreatePackage leaves -1, which the engine uses unchecked as a table index (39213, 21521).
template <class TCreate, class TFill> [[nodiscard]] auto CreateDoNothing(TCreate&& aCreate, TFill&& aFill) noexcept
{
    auto* pPackage = aCreate();
    if (pPackage)
    {
        pPackage->packFlags |= 4;
        aFill(pPackage);
    }
    return pPackage;
}
} // namespace CopyPackage
