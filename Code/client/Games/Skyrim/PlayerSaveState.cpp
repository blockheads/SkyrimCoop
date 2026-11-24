#include <PlayerSaveState.h>

#include <Camera/PlayerCamera.h>
#include <Effects/ActiveEffect.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <ExtraData/ExtraHotkey.h>
#include <FormIds.h>
#include <Forms/ActorValueInfo.h>
#include <Forms/MagicItem.h>
#include <Forms/SpellItem.h>
#include <Games/FormState.h>
#include <Games/ItemChanges.h>
#include <Games/Overrides.h>
#include <ItemIds.h>
#include <Magic/MagicCaster.h>
#include <PlayerCharacter.h>
#include <Utils.h>

namespace
{
constexpr uint32_t kDamaged[] = {ActorValueInfo::kHealth, ActorValueInfo::kMagicka, ActorValueInfo::kStamina};
constexpr uint32_t kDispelled = 0x40000; // ActiveEffect flags, set by Dispel (34061)

// A timed effect still running: the ones a save's player carries beyond its abilities and worn enchantments.
bool Timed(const ActiveEffect& acEffect) noexcept
{
    return acEffect.pSpell && acEffect.fDuration > 0.f && acEffect.fElapsedSeconds < acEffect.fDuration && !(acEffect.uiFlags & kDispelled);
}

std::optional<uint8_t> EffectIndex(const ActiveEffect& acEffect) noexcept
{
    const auto& cEffects = acEffect.pSpell->listOfEffects;
    for (uint32_t i = 0; i < cEffects.length && i < 0xFF; ++i)
        if (cEffects[i] == acEffect.pEffect)
            return static_cast<uint8_t>(i);
    return std::nullopt;
}

void ReadFavoriteItems(PlayerCharacter& aPlayer, PlayerOwnState& aState) noexcept
{
    auto* pChanges = aPlayer.GetContainerChanges();
    if (!pChanges || !pChanges->entries)
        return;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form && pEntry->dataList)
            for (ExtraDataList* pList : *pEntry->dataList)
                if (const auto* cpHotkey = pList ? static_cast<const ExtraHotkey*>(pList->GetByType(ExtraDataType::Hotkey)) : nullptr)
                {
                    PlayerOwnState::FavoriteItem item{};
                    item.Stack.BaseId = ItemId(pEntry->form->formID).value_or(GameId{});
                    {
                        ScopedExtraDataOverride _;
                        TESObjectREFR::GetItemFromExtraData(item.Stack, pList);
                    }
                    item.Hotkey = cpHotkey->hotkey;
                    aState.FavoriteItems.push_back(std::move(item));
                }
}

// Each favorite on the stack the record names, as the item menus favorite one: a one-list entry for the stack (16098).
void ApplyFavoriteItems(PlayerCharacter& aPlayer, const PlayerOwnState& acState) noexcept
{
    auto* pChanges = aPlayer.GetContainerChanges();
    if (!pChanges || !pChanges->entries)
        return;
    for (const PlayerOwnState::FavoriteItem& cItem : acState.FavoriteItems)
    {
        auto* pObject = Cast<TESBoundObject>(TESForm::GetById(ItemForm(cItem.Stack.BaseId)));
        ExtraContainerChanges::Entry* pEntry = nullptr;
        for (auto* pCandidate : *pChanges->entries)
            if (pCandidate && pCandidate->form == pObject)
                pEntry = pCandidate;
        if (!pObject || !pEntry)
            continue;
        ExtraDataList* pList = aPlayer.FindCarriedStack(*pObject, cItem.Stack);
        GameList<ExtraDataList> lists{};
        lists.entry.data = pList;
        ExtraContainerChanges::Entry stack{pObject, pList ? &lists : nullptr, pEntry->count};
        pChanges->SetFavorite(&stack, pList);
        if (cItem.Hotkey >= 0)
            if (ExtraDataList* pFavorite = pEntry->GetFavoriteList())
                pFavorite->SetHotkey(cItem.Hotkey);
    }
}

// The save's player's timed effects go, the record's are cast again on the player and set to the time they ran.
void ApplyEffects(PlayerCharacter& aPlayer, const PlayerOwnState& acState) noexcept
{
    if (GameList<ActiveEffect>* pEffects = aPlayer.GetActiveEffects())
        for (ActiveEffect* pEffect : *pEffects)
            if (pEffect && Timed(*pEffect))
                pEffect->Dispel(true);
    MagicCaster* pCaster = aPlayer.GetMagicCaster(MagicSystem::INSTANT);
    std::vector<MagicItem*> cast;
    for (const PlayerOwnState::Effect& cEffect : acState.Effects)
    {
        auto* pSpell = Cast<MagicItem>(TESForm::GetById(ToGame(cEffect.Spell)));
        if (!pSpell || !pCaster || std::ranges::find(cast, pSpell) != cast.end())
            continue;
        cast.push_back(pSpell);
        pCaster->CastSpellImmediate(pSpell, false, &aPlayer, 1.0f, false, 0.0f);
    }
    if (GameList<ActiveEffect>* pEffects = aPlayer.GetActiveEffects())
        for (ActiveEffect* pEffect : *pEffects)
            if (pEffect && Timed(*pEffect))
                for (const PlayerOwnState::Effect& cEffect : acState.Effects)
                    if (ToSession(pEffect->pSpell->formID) == cEffect.Spell && EffectIndex(*pEffect) == cEffect.Index)
                        pEffect->fElapsedSeconds = cEffect.Elapsed;
}
} // namespace

namespace PlayerSaveState
{
PlayerOwnState Read(PlayerCharacter& aPlayer) noexcept
{
    PlayerOwnState state;
    state.Present = true;
    ReadFavoriteItems(aPlayer, state);
    for (const uint32_t cType : PlayerOwnState::kGlobals)
        if (std::string data; FormState::SaveGlobal(cType, data))
            state.Globals[cType] = std::move(data);
    for (size_t i = 0; i < std::size(kDamaged); ++i)
        state.Damage[i] = aPlayer.GetActorValueModifier(ActorValueOwner::ForceMode::DAMAGE, kDamaged[i]);
    if (GameList<ActiveEffect>* pEffects = aPlayer.GetActiveEffects())
        for (const ActiveEffect* cpEffect : *pEffects)
            if (cpEffect && Timed(*cpEffect))
                if (const auto cSpell = ToSession(cpEffect->pSpell->formID); cSpell) // a player-made potion's: none
                    if (const auto cIndex = EffectIndex(*cpEffect))
                        state.Effects.push_back({*cSpell, *cIndex, cpEffect->fElapsedSeconds});
    for (const auto* cpCooldown = aPlayer.powerCooldowns; cpCooldown; cpCooldown = cpCooldown->pNext)
        if (cpCooldown->pItem)
            if (const auto cSpell = ToSession(cpCooldown->pItem->formID))
                state.Cooldowns.push_back({*cSpell, cpCooldown->seconds});
    state.Trainings = aPlayer.skillTrainingsThisLevel;
    if (PlayerCamera* pCamera = PlayerCamera::Get())
        state.FirstPerson = pCamera->IsFirstPerson();
    return state;
}

void Apply(PlayerCharacter& aPlayer, const PlayerOwnState& acState) noexcept
{
    if (!acState.Present)
        return;
    ApplyFavoriteItems(aPlayer, acState);
    for (const auto& [cType, cData] : acState.Globals)
        if (!FormState::LoadGlobal(cType, cData))
            spdlog::warn("PlayerSaveState: save global {} did not load ({} bytes)", cType, cData.size());
    ApplyEffects(aPlayer, acState);
    // After the effects, whose fortifications set the maximum the damage counts from.
    ActorValueOwner& values = EngineVirtual(aPlayer.actorValueOwner);
    for (size_t i = 0; i < std::size(kDamaged); ++i)
        if (const float cDelta = acState.Damage[i] - aPlayer.GetActorValueModifier(ActorValueOwner::ForceMode::DAMAGE, kDamaged[i]); cDelta != 0.f)
            values.ForceCurrent(ActorValueOwner::ForceMode::DAMAGE, kDamaged[i], cDelta);
    aPlayer.ClearPowerCooldowns();
    for (const PlayerOwnState::Cooldown& cCooldown : acState.Cooldowns)
        if (auto* pPower = Cast<SpellItem>(TESForm::GetById(ToGame(cCooldown.Spell))))
            aPlayer.SetPowerCooldown(pPower, cCooldown.Seconds);
    aPlayer.skillTrainingsThisLevel = acState.Trainings;
    if (PlayerCamera* pCamera = PlayerCamera::Get(); pCamera && pCamera->IsFirstPerson() != acState.FirstPerson)
        acState.FirstPerson ? pCamera->ForceFirstPerson() : pCamera->ForceThirdPerson();
}
} // namespace PlayerSaveState
