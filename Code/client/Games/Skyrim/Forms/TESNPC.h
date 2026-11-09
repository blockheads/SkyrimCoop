#pragma once

#include <Forms/TESActorBase.h>

#include <Components/TESRaceForm.h>
#include <Components/BGSOverridePackCollection.h>
#include <Games/Primitives.h>

struct BGSColorForm;
struct BGSTextureSet;
struct TESClass;
struct TESCombatStyle;
struct TESObjectARMO;
struct BGSOutfit;
struct BGSHeadPart;
struct BGSRelationship;

struct TESNPC : TESActorBase
{
    static constexpr FormType Type = FormType::Npc;

    static TESNPC* Create(const std::string& acBuffer, uint32_t aChangeFlags) noexcept;

    TESNPC* GetTemplateBase() const noexcept
    {
        TESNPC* pTemplate = npcTemplate;

        while (pTemplate && pTemplate->IsTemporary())
            pTemplate = pTemplate->npcTemplate;

        return pTemplate;
    }

    struct FaceMorphs
    {
        float option[19];
        uint32_t presets[4];

        void CopyFrom(const FaceMorphs& acRhs)
        {
            std::copy(std::begin(acRhs.option), std::end(acRhs.option), std::begin(option));
            std::copy(std::begin(acRhs.presets), std::end(acRhs.presets), std::begin(presets));
        }
    };

    struct HeadData
    {
        BGSColorForm* hairColor;
        BGSTextureSet* headTexture;
    };

    TESRaceForm raceForm;
    BGSOverridePackCollection overridePacks;
    void* unkDC;
    uint8_t unk0E0[0x24];
    uint8_t pad1B4[0x6];
    uint16_t unk10A;
    TESClass* npcClass;

    HeadData* headData;
    uintptr_t unk114;
    TESCombatStyle* combatStyle;
    size_t unk11C;
    TESRace* overlayRace;
    TESNPC* npcTemplate;
    float height;
    float weight;
    uintptr_t unk130;
    BSFixedString shortName;
    TESObjectARMO* farArmo;
    BGSOutfit* outfits[2];
    uintptr_t unk144;
    TESFaction* faction;

    BGSHeadPart** headparts;
    uint8_t headpartsCount;

    uint8_t pad241[5];

    struct Color
    {
        uint8_t red, green, blue;
    } color;

    // A tint layer over its race's tint asset of that index (CommonLibSSE-NG TESNPC::Layer).
    struct TintLayer
    {
        Color tintColor;            // 00, with a byte of padding (TINC)
        uint8_t pad03;
        uint16_t tintIndex;          // 04 (TINI)
        uint16_t preset;             // 06 (TIAS)
        uint16_t interpolationValue; // 08, alpha * 100 (TINV)
        uint16_t pad0A;
        uint32_t pad0C;
    };
    static_assert(sizeof(TintLayer) == 0x10);

    GameArray<BGSRelationship*>* relationships;
    FaceMorphs* faceMorphs;
    GameArray<TintLayer*>* tintLayers; // 260, read by TESNPC::GetTintMasks (1403C8870)

    BGSHeadPart* GetHeadPart(uint32_t aType);
    // The part its head is built with: a base overlay's when it has overlays (a vampire keeps its old face,
    // TESNPC::HasOverlays 24790), else its own (CommonLibSSE-NG GetCurrentHeadPartByType).
    BGSHeadPart* GetCurrentHeadPart(uint32_t aType);
    void Serialize(std::string* apSaveBuffer) const noexcept;
    void Deserialize(const std::string& acBuffer, uint32_t aChangeFlags) noexcept;
    void Initialize() noexcept;
};

static_assert(offsetof(TESNPC, npcClass) == 0x1C0);
static_assert(offsetof(TESNPC, npcTemplate) == 0x1F0); // CommonLibSSE-NG faceNPC
static_assert(offsetof(TESNPC, weight) == 0x1FC);
static_assert(offsetof(TESNPC, tintLayers) == 0x260);
static_assert(offsetof(TESNPC, color) == 0x246);
static_assert(offsetof(TESNPC, relationships) == 0x250);
