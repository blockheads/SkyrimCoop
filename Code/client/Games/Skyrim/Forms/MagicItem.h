#pragma once

#include <Forms/TESBoundObject.h>
#include <Forms/EffectSetting.h>
#include <Forms/BGSKeyword.h>
#include <Magic/EffectItem.h>
#include <Components/BGSKeywordForm.h>
#include <Components/TESFullName.h>
#include <Games/Primitives.h>
#include <NoCopyMove.h>
#include <Structs/Inventory.h>

#include <optional>
#include <vector>

struct MagicItem : TESBoundObject
{
    bool IsWardSpell() const noexcept;
    bool IsInvisibilitySpell() const noexcept;
    bool IsHealingSpell() const noexcept;
    bool IsBuffSpell() const noexcept;
    bool IsBoundWeaponSpell() noexcept;

    EffectItem* GetEffect(const uint32_t aEffectId) noexcept;
    // Its effects as the session names them (a player-made item's, made again from them elsewhere); none when one cannot
    // be named (no effect, a created one).
    [[nodiscard]] std::optional<std::vector<Inventory::EffectItem>> SessionEffects() const noexcept;

    TESFullName fullName;
    BGSKeywordForm keyword;
    GameArray<EffectItem*> listOfEffects;
    int32_t iHostileCount;
    EffectSetting* pAVEffectSetting;
    uint32_t uiPreloadCount;
    void* pPreloadItem;
};

static_assert(sizeof(MagicItem) == 0x90);

// Effects for a new magic item as BGSCreatedObjectManager's adds take them (they copy them), freed with it.
struct NewEffects
{
    NewEffects() noexcept = default;
    ~NewEffects() noexcept;
    NO_COPY_MOVE(NewEffects);

    // False when an effect is not in this game.
    [[nodiscard]] bool Set(const std::vector<Inventory::EffectItem>& acEffects) noexcept;

    GameArray<EffectItem> Array{};
};
