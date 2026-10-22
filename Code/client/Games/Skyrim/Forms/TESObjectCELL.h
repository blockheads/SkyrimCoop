#pragma once

#include <Forms/TESForm.h>

struct TESObjectREFR;
struct TESWorldSpace;
struct BGSEncounterZone;
struct LoadedCellData;
struct NavMeshArray;
#include <ExtraData/ExtraDataList.h>

struct TESObjectCELL : TESForm
{
    Vector<TESObjectREFR*> GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept;
    // Every reference in the cell, taken under its lock (as the engine's own walks do, e.g. OnCellLoad's 53964).
    [[nodiscard]] Vector<TESObjectREFR*> GetReferences() noexcept;
    void GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept;

    bool IsValid() const { return cellFlags[4] == 7; }
    bool IsInterior() const { return (cellFlags[0] & 1) != 0; }
    // Its grid cell outdoors (cellData's XCLC, CommonLibSSE-NG EXTERIOR_DATA); false indoors.
    bool GetGrid(int32_t& aX, int32_t& aY) const noexcept;
    // Attached and done loading (a reset included): what its references hold is this game's state.
    bool IsFullyLoaded() const { return IsValid() && loadedCellData && loadedCellData->fullyLoaded; }

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
        uint8_t pad168[0x17C - 0x168];
        bool fullyLoaded; // 17C: TESCellFullyLoadedEvent sent (19146), its references all in
    };
    SKYRIM_STRUCT_ASSERT(offsetof(LoadedCellData, encounterZone) == 0x160);
    SKYRIM_STRUCT_ASSERT(offsetof(LoadedCellData, fullyLoaded) == 0x17C);

    uint8_t pad20[0x40 - 0x20];
    uint8_t cellFlags[5];
    bool autoWaterLoaded;
    bool cellDetached;
    uint8_t pad47;
    ExtraDataList extraData;
    uint64_t cellData;
    void* pCellLand;
    float waterHeight;
    NavMeshArray* pNavMeshes;
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
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, pNavMeshes) == 0x80);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, refData) == 0x88);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, lock) == 0x120);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, worldspace) == 0x128);
SKYRIM_STRUCT_ASSERT(offsetof(TESObjectCELL, loadedCellData) == 0x130);
SKYRIM_STRUCT_ASSERT(sizeof(TESObjectCELL) == 0x148);
