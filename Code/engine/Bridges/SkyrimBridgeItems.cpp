// SkyrimBridge: an actor's items (IActorItems).
#include "SkyrimBridgeInternal.h"

bool SkyrimBridge::IsWeaponDrawn(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->actorState.IsWeaponDrawn();
    return false;
}

uint32_t SkyrimBridge::GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept
{
    // What the actor's hands hold: AIProcess::equippedObjects [0 left, 1 right] (CommonLibSSE).
    Actor* pActor = GetActor(aActorFormId);
    if (!pActor || !pActor->currentProcess || aSlot > 1)
        return 0;
    const TESForm* pObject = pActor->currentProcess->equippedObject[aSlot];
    return pObject ? pObject->formID : 0;
}

void SkyrimBridge::SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept
{
    Breadcrumbs::Record("SetActorWeaponDrawnEx", aFormId, static_cast<uint32_t>(aDraw));
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetWeaponDrawnEx(aDraw);
}

void SkyrimBridge::ShowActorWeaponDrawn(uint32_t aFormId, bool aDrawn) noexcept
{
    Breadcrumbs::Record("ShowActorWeaponDrawn", aFormId, static_cast<uint32_t>(aDrawn));
    if (Actor* pActor = GetActor(aFormId))
        pActor->ShowWeaponDrawn(aDrawn);
}

WeaponState SkyrimBridge::GetWeaponState(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return WeaponState::Sheathed;
    return static_cast<WeaponState>(pActor->actorState.GetWeaponState());
}
