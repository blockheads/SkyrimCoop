#pragma once

#include <Forms/TESForm.h>

struct TESObjectREFR;
struct TESWorldSpace;
struct BGSEncounterZone;
struct LoadedCellData;

struct TESObjectCELL : TESForm
{
    Vector<TESObjectREFR*> GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept;
    void GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept;

    bool IsValid() const { return cellFlags[4] == 7; }

    struct ReferenceData
    {
        struct Reference
        {
            TESObjectREFR* ref;
            void* unk08;

            TESObjectREFR* Get() { return unk08 != nullptr ? ref : nullptr; }
        };

        uint64_t unk0;
        uint32_t unk8;
        uint32_t capacity;
        uint32_t available;
        uint32_t unkC;
        void* unk10;
        void* unk18;
        Reference* refArray;

        uint32_t Count() { return capacity - available; }
    };
    SKYRIM_STRUCT_ASSERT(sizeof(ReferenceData) == 0x30);

    struct LoadedCellData
    {
        uint8_t pad0[0x160];
        BGSEncounterZone* encounterZone;
    };
    SKYRIM_STRUCT_ASSERT(offsetof(LoadedCellData, encounterZone) == 0x160);

    uint8_t pad20[0x40 - 0x20];
    uint8_t cellFlags[5];
    bool autoWaterLoaded;
    bool cellDetached;
    uint8_t pad47;
    ExtraDataList extraData;
    uint64_t cellData;
    void* pCellLand;
    float waterHeight;
    void* pNavMeshes;
    ReferenceData refData;
    void* pUnkB8;
    GameArray<TESObjectREFR*> objectList;
    GameArray<void*> unkD8;
    GameArray<void*> unkF0;
    GameArray<void*> unk108;
    BSRecursiveLock lock;
    TESWorldSpace* worldspace;
    LoadedCellData* loadedCellData;
    void* pLightingTemplate;
    uint64_t unk140;
};

SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, cellFlags) == 0x40);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, refData) == 0x88);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, worldspace) == 0x128);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, loadedCellData) == 0x130);
SKYRIM_STRUCT_ASSERT(sizeof(TESObjectCELL) == 0x148);
