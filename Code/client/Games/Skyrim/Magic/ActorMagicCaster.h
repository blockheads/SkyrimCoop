#pragma once

#include "MagicCaster.h"

#include <Games/Skyrim/NetImmerse/NiPointer.h>

struct Actor;
struct NiNode;
struct BGSArtObject;
struct WeaponEnchantmentController;

struct ActorMagicCaster : MagicCaster
{
    virtual uint64_t SpellCast(bool abSuccess, uint32_t auiTargetCount, MagicItem* apSpell) override;

    virtual void sub_1D(float aUnk1);

    uint8_t unk48[0x70];
    Actor* pCasterActor;
    NiNode* pMagicNode;
    NiPointer<uint64_t> spLight; // NiPointer<BSLight> spLight;
    void(__fastcall* pfInterruptHandler)(Actor*);
    uint64_t LoadGameSubBuffer; // BGSLoadGameSubBuffer LoadGameSubBuffer;
    BGSArtObject* pWaitingForCastingArt;
    WeaponEnchantmentController* pWeaponEnchantmentController;
    float fCostCharged;
    MagicSystem::CastingSource eCastingSource;
    uint32_t uiFlags;

    // uiFlags: the cast is free and unchecked: CheckCast (34145) passes, Update (34143) spends nothing.
    // Cleared by FinishCastImpl/InterruptCastImpl (CommonLib Flags::kSkipCheckCast).
    static constexpr uint32_t kSkipCheckCast = 1u << 1;
};

static_assert(sizeof(ActorMagicCaster) == 0x100);
static_assert(offsetof(ActorMagicCaster, fCostCharged) == 0xF0);
static_assert(offsetof(ActorMagicCaster, uiFlags) == 0xF8);
