#pragma once

struct TESForm;
struct NiAVObject;

// The temp effects ProcessLists keeps for magic: an effect shader or an art object played on a reference
// (1.6.1170, layouts from CommonLibSSE-NG). They are NiObjects: use Cast<> to tell them apart.
struct BSTempEffect
{
    uint8_t pad0[0x30];
};

// What plays a reference effect: an active effect, a script's VisualEffect, the sky (CommonLibSSE-NG).
struct ReferenceEffectController
{
    virtual ~ReferenceEffectController();

    virtual void sub_01();
    virtual void sub_02();
    virtual void sub_03();
    virtual void sub_04();
    virtual void sub_05();
    virtual void sub_06();
    virtual void sub_07();
    virtual void sub_08();
    virtual void sub_09();
    virtual void sub_0A();
    virtual void sub_0B();
    virtual void sub_0C();
    virtual void sub_0D();
    virtual void sub_0E();
    virtual void sub_0F();
    virtual void sub_10();
    virtual void sub_11();
    virtual void sub_12();
    virtual void sub_13();
    virtual void sub_14();
    virtual void sub_15();
    virtual void sub_16();
    virtual void sub_17();
    virtual void sub_18();
    virtual void sub_19();
    // 1A: plays at the camera, not on its reference (OwnedCameraEffectController, SkyEffectController)
    virtual bool EffectAttachesToCamera();
};

struct ReferenceEffect : BSTempEffect
{
    ReferenceEffectController* pController; // 30
    uint32_t hTarget;    // 38 - the reference it plays on
    uint32_t hAimTarget; // 3C
    bool bFinished;      // 40 - stopping sets it; the effect then fades out and leaves the list
    uint8_t pad41[0x48 - 0x41];
};

struct ModelReferenceEffect : ReferenceEffect
{
    uint8_t pad48[0xB8 - 0x48];
    TESForm* pArtObject;     // B8 - BGSArtObject
    void* pCloneTask;        // C0 - the art's model while it loads
    NiAVObject* pArtObject3D; // C8 - the art's model, once loaded
    uint32_t uiFlags;        // D0 - 1 attached, 2 third person
    uint32_t padD4;
};

struct ShaderReferenceEffect : ReferenceEffect
{
    uint8_t pad48[0x108 - 0x48];
    TESForm* pEffectShader; // 108 - TESEffectShader
};

static_assert(offsetof(ReferenceEffect, hTarget) == 0x38);
static_assert(offsetof(ReferenceEffect, bFinished) == 0x40);
static_assert(offsetof(ModelReferenceEffect, pArtObject) == 0xB8);
static_assert(offsetof(ModelReferenceEffect, pArtObject3D) == 0xC8);
static_assert(sizeof(ModelReferenceEffect) == 0xD8);
static_assert(offsetof(ShaderReferenceEffect, pEffectShader) == 0x108);
