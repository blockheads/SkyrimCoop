#pragma once

#include <Forms/TESForm.h>
#include <Components/TESFullName.h>
#include <Components/BGSKeywordForm.h>
#include <Components/BGSMenuDisplayObject.h>
#include <Games/Magic/MagicSystem.h>

struct EffectSetting : TESForm
{
    TESFullName fullName;
    BGSMenuDisplayObject menuDisplayObject;
    BGSKeywordForm keywordForm;
    uintptr_t unk30;
    uintptr_t unk34;
    // CommonLibSSE-NG EffectSettingData (+0x68) from here.
    enum Flags : uint32_t
    {
        kFXPersist = 1u << 12,   // the hit visuals last as long as the effect, else they play once
        kNoHitEffect = 1u << 27,
    };
    uint32_t flags;
    uint32_t unk3C;
    void* unk40;
    uint32_t unk44;
    uint32_t unk48;
    uint16_t unk4C;
    uint8_t pad4E[2];
    void* unk50;
    uint32_t unk54;
    TESForm* pHitShader; // effectShader
    void* unk5C;
    uint8_t unk60[0x18];
    EffectArchetypes::ArchetypeID eArchetype;
    void* unk80[2];
    uint32_t castType;
    uint32_t deliveryType;
    uint32_t secondaryAV;
    TESForm* pCastingArt;
    TESForm* pHitArt; // hitEffectArt
    // more stuff
};

static_assert(offsetof(EffectSetting, flags) == 0x68);
static_assert(offsetof(EffectSetting, pHitShader) == 0x98);
static_assert(offsetof(EffectSetting, eArchetype) == 0xC0);
static_assert(offsetof(EffectSetting, castType) == 0xD8 && offsetof(EffectSetting, deliveryType) == 0xDC);
static_assert(offsetof(EffectSetting, pHitArt) == 0xF0);
