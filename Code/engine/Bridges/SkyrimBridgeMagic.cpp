// SkyrimBridge: an actor's spells and effects (IActorMagic).
#include "SkyrimBridgeInternal.h"

void SkyrimBridge::GenerateMagicCasters(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("GenerateMagicCasters", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        pActor->GenerateMagicCasters();
}

void SkyrimBridge::SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept
{
    Breadcrumbs::Record("SetMagicCasterDualCasting", aFormId, aCastingSource);
    if (Actor* pActor = GetActor(aFormId))
    {
        using CS = MagicSystem::CastingSource;
        MagicCaster* pCaster = pActor->GetMagicCaster(static_cast<CS>(aCastingSource));
        if (pCaster)
            pCaster->SetDualCasting(aDual);
    }
}

void SkyrimBridge::CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                             uint32_t aDesiredTargetFormId) noexcept
{
    Breadcrumbs::Record("CastSpell", aActorFormId, aSpellFormId);
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    MagicCaster* pCaster = GetCaster(GetActor(aActorFormId), aCastingSource);
    if (!pSpell || !pCaster)
    {
        spdlog::warn("{}: no spell {:X} or caster {} for actor {:X}", __FUNCTION__, aSpellFormId, aCastingSource, aActorFormId);
        return;
    }
    TESObjectREFR* pTarget = aDesiredTargetFormId ? Cast<TESObjectREFR>(TESForm::GetById(aDesiredTargetFormId)) : nullptr;
    pCaster->CastSpellImmediate(pSpell, false, pTarget, 1.0f, false, 0.0f);
}

void SkyrimBridge::InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept
{
    Breadcrumbs::Record("InterruptCast", aFormId, aCastingSource);
    if (MagicCaster* pCaster = GetCaster(GetActor(aFormId), aCastingSource))
        pCaster->InterruptCast();
}

uint32_t SkyrimBridge::GetCastingSpellFormId(uint32_t aFormId, uint32_t aCastingSource) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || aCastingSource >= 4 || !pActor->casters[aCastingSource])
        return 0;
    MagicItem* pSpell = pActor->casters[aCastingSource]->pCurrentSpell;
    return pSpell ? pSpell->formID : 0;
}

void SkyrimBridge::RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept
{
    Breadcrumbs::Record("RemoveSpell", aActorFormId, aSpellFormId);
    Actor* pActor = GetActor(aActorFormId);
    if (!pActor)
        return;
    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(aSpellFormId));
    if (pSpell)
        pActor->RemoveSpell(pSpell);
}

uint32_t SkyrimBridge::GetEquippedShoutFormId(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->equippedShout ? pActor->equippedShout->formID : 0;
    return 0;
}

uint32_t SkyrimBridge::GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        return pActor->magicItems[aSlot] ? pActor->magicItems[aSlot]->formID : 0;
    return 0;
}

std::vector<IActorBridge::ActiveEffectInfo> SkyrimBridge::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> result;
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return result;

    // MagicTarget::GetActiveEffectList is vtable slot 7 (CommonLibSSE); the list is a
    // BSSimpleList<ActiveEffect*>: an inline head node {item, next}.
    struct Node
    {
        ActiveEffect* pItem;
        Node* pNext;
    };
    MagicTarget* pTarget = &pActor->magicTarget;
    using TGetList = Node* (*)(MagicTarget*);
    const auto cGetList = reinterpret_cast<TGetList>((*reinterpret_cast<void***>(pTarget))[7]);
    for (Node* pNode = cGetList(pTarget); pNode; pNode = pNode->pNext)
    {
        ActiveEffect* pEffect = pNode->pItem;
        if (!pEffect)
            continue;
        ActiveEffectInfo info;
        auto* pEffectItem = static_cast<EffectItem*>(pEffect->pEffect);
        info.EffectFormId = pEffectItem && pEffectItem->pEffectSetting ? pEffectItem->pEffectSetting->formID : 0;
        info.SpellFormId = pEffect->pSpell ? pEffect->pSpell->formID : 0;
        if (TESObjectREFR* pCaster = TESObjectREFR::GetByHandle(pEffect->hCaster))
        {
            info.CasterFormId = pCaster->formID;
            info.CasterIsActor = Cast<Actor>(pCaster) != nullptr;
        }
        info.Magnitude = pEffect->fMagnitude;
        info.Duration = pEffect->fDuration;
        info.Elapsed = pEffect->fElapsedSeconds;
        info.Flags = pEffect->uiFlags;
        result.push_back(info);
    }
    return result;
}

void SkyrimBridge::ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept
{
    Breadcrumbs::Record("ActorEquipSpell", aActorFormId, aSpellFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pSpell = TESForm::GetById(aSpellFormId);
    if (pActor && pSpell)
        EquipManager::Get()->EquipSpell(pActor, pSpell, static_cast<int>(aSlotId));
}

void SkyrimBridge::ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept
{
    Breadcrumbs::Record("ActorEquipShout", aActorFormId, aShoutFormId);
    Actor* pActor = GetActor(aActorFormId);
    TESForm* pShout = TESForm::GetById(aShoutFormId);
    if (pActor && pShout)
        EquipManager::Get()->EquipShout(pActor, pShout);
}

Inventory SkyrimBridge::GetActorInventory(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    return pActor->GetActorInventory();
}

void SkyrimBridge::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    Breadcrumbs::Record("SetActorInventory", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SetActorInventory(acInventory);
}

bool SkyrimBridge::EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept
{
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    return cVisuals.pShader || cVisuals.pArt;
}

bool SkyrimBridge::PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept
{
    Breadcrumbs::Record("PlayEffectHitVisuals", aFormId, aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetNiNode())
        return false;
    ApplyHitVisuals(pActor, GetHitVisuals(aEffectFormId), -1.f);
    return IsPlayingEffectHitVisuals(aFormId, aEffectFormId);
}

bool SkyrimBridge::FlashEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept
{
    Breadcrumbs::Record("FlashEffectHitVisuals", aFormId, aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId, false);
    if (!pActor || !pActor->GetNiNode() || !(cVisuals.pShader || cVisuals.pArt))
        return false;
    // A lifetime of 0: finished at once, as an effect's that does not persist, so each runs its own pass and ends
    // (ReferenceEffect 34769, ShaderReferenceEffect::Update 34913, the shader's pass 17339).
    ApplyHitVisuals(pActor, cVisuals, 0.f);
    return true;
}

bool SkyrimBridge::IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept
{
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    const auto cPlaying = pActor && pProcessLists ? pProcessLists->GetPlayingVisualFormIds(pActor->GetHandle().handle.iBits)
                                                  : std::vector<uint32_t>{};
    const auto cIsPlaying = [&](const TESForm* apForm) {
        return !apForm || std::binary_search(cPlaying.begin(), cPlaying.end(), apForm->formID);
    };
    return (cVisuals.pShader || cVisuals.pArt) && cIsPlaying(cVisuals.pShader) && cIsPlaying(cVisuals.pArt);
}

void SkyrimBridge::SetCopyEffects(uint32_t aFormId, std::vector<OwnerEffects::Held> aHeld) noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy())
        return;
    pActor->GetExtension()->OwnerEffects.store(std::make_shared<const std::vector<OwnerEffects::Held>>(std::move(aHeld)));
}

void SkyrimBridge::StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept
{
    Breadcrumbs::Record("StopEffectHitVisuals", aFormId, aEffectFormId);
    Actor* pActor = GetActor(aFormId);
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pActor || !pProcessLists)
        return;
    const HitVisuals cVisuals = GetHitVisuals(aEffectFormId);
    if (cVisuals.pShader)
        pProcessLists->StopEffectShader(pActor, cVisuals.pShader);
    if (cVisuals.pArt)
        pProcessLists->StopArtObject(pActor, cVisuals.pArt);
}
