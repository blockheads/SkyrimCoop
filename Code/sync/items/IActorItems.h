#pragma once

#include <Structs/WeaponState.h>
#include <cstdint>

// What an actor carries and holds. One part of IActorBridge (ports/IActorBridge.h).
struct IActorItems
{
    virtual ~IActorItems() = default;

    virtual bool IsWeaponDrawn(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept = 0;
    virtual void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept = 0;
    // The draw or sheathe's result with no action or animation: for a copy whose graph is already its owner's.
    virtual void ShowActorWeaponDrawn(uint32_t aFormId, bool aDrawn) noexcept = 0;
    virtual WeaponState GetWeaponState(uint32_t aFormId) const noexcept = 0;
};
