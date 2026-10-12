#pragma once

struct TESForm;
struct MiddleProcess;
struct HighProcessData;
struct TESAmmo;

struct AIProcess
{
    bool SetCurrentAmmo(TESAmmo* apAmmo) noexcept;

    void KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude);

    void* unk0;
    MiddleProcess* middleProcess;
    HighProcessData* highProcess; // 10 (CommonLib high)
    void* packageLock;
    struct TESPackage* package;
    uint32_t unk10[7];
    uint32_t unk34[8];
    float unk54;
    uint32_t unk58[4];
    TESForm* equippedObject[2];

    uint8_t pad88[0x137 - 0x88];

    int8_t processLevel; // 0 high .. 3 low (CommonLib)
};


struct HighProcessData
{
    uint8_t pad0[0x218];
    char* strVoiceSubtitle;
    GameArray<std::tuple<uint32_t, void*>> KnowledgeArray; // BSTuple, std::tuple is prolly wrong
};

SKYRIM_STRUCT_ASSERT(offsetof(AIProcess, highProcess) == 0x10);

SKYRIM_STRUCT_ASSERT(offsetof(AIProcess, processLevel) == 0x137);
