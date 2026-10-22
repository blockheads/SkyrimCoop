#pragma once

#include <Actor.h>
#include <Games/Primitives.h>

struct TESForm;
struct MiddleProcess;
struct HighProcessData;
struct TESAmmo;

// The package an actor runs (CommonLibSSE-NG ActorPackage), set by ActorPackage::InitFromPackage (38959).
struct ActorPackage
{
    uint32_t lockOwner; // BSSpinLock
    uint32_t lockCount;
    struct TESPackage* package; // 08
    void* data;                 // 10 ActorPackageData, made for the package
    uint32_t target;            // 18 ObjectRefHandle
    int32_t currentProcedureIndex;
    float packageStartTime;
    uint32_t modifiedPackageFlag;
    uint16_t modifiedInterruptFlag;
    int8_t actorPackageFlags;
    int8_t preferredSpeed;
    uint32_t pad2C;
};
static_assert(sizeof(ActorPackage) == 0x30);

struct AIProcess
{
    bool SetCurrentAmmo(TESAmmo* apAmmo) noexcept;

    void KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude);

    void* unk0;
    MiddleProcess* middleProcess; // 08 (CommonLib middleHigh)
    HighProcessData* highProcess; // 10 (CommonLib high)
    ActorPackage currentPackage;  // 18
    uint8_t pad48[0xF0 - 0x48];
    TESForm* equippedObject[2]; // F0
    uint8_t pad100[0x110 - 0x100];
    uint32_t followTarget; // 110 RefHandle
    uint32_t target;       // 114 RefHandle
    uint8_t pad118[0x137 - 0x118];

    int8_t processLevel; // 0 high .. 3 low (CommonLib)
};

// BSSoundHandle (CommonLibSSE-NG): the audio system's id for a playing sound.
struct BSSoundHandle
{
    uint32_t soundID; // -1 none
    bool assumeSuccess;
    uint32_t state;
};

struct HighProcessData
{
    uint8_t pad0[0x218];
    char* strVoiceSubtitle;
    GameArray<std::tuple<uint32_t, void*>> KnowledgeArray; // BSTuple, std::tuple is prolly wrong
    uint8_t pad238[0x2FC - 0x238];
    BSSoundHandle soundHandles[2]; // [0] the voice line (CommonLib)
};

static_assert(sizeof(BSSoundHandle) == 0xC);
static_assert(offsetof(HighProcessData, soundHandles) == 0x2FC);
static_assert(offsetof(AIProcess, highProcess) == 0x10);
static_assert(offsetof(AIProcess, currentPackage) == 0x18);
static_assert(offsetof(AIProcess, equippedObject) == 0xF0);
static_assert(offsetof(AIProcess, followTarget) == 0x110);

static_assert(offsetof(AIProcess, processLevel) == 0x137);
