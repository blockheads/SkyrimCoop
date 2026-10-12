#pragma once

struct TESForm;

// The temp effects ProcessLists keeps for magic: an effect shader or an art object played on a reference
// (1.6.1170, layouts from CommonLibSSE-NG). They are NiObjects: use Cast<> to tell them apart.
struct BSTempEffect
{
    uint8_t pad0[0x30];
};

struct ReferenceEffect : BSTempEffect
{
    void* pController;   // 30
    uint32_t hTarget;    // 38 - the reference it plays on
    uint32_t hAimTarget; // 3C
    bool bFinished;      // 40 - stopping sets it; the effect then fades out and leaves the list
    uint8_t pad41[0x48 - 0x41];
};

struct ModelReferenceEffect : ReferenceEffect
{
    uint8_t pad48[0xB8 - 0x48];
    TESForm* pArtObject; // B8 - BGSArtObject
};

struct ShaderReferenceEffect : ReferenceEffect
{
    uint8_t pad48[0x108 - 0x48];
    TESForm* pEffectShader; // 108 - TESEffectShader
};

static_assert(offsetof(ReferenceEffect, hTarget) == 0x38);
static_assert(offsetof(ReferenceEffect, bFinished) == 0x40);
static_assert(offsetof(ModelReferenceEffect, pArtObject) == 0xB8);
static_assert(offsetof(ShaderReferenceEffect, pEffectShader) == 0x108);
