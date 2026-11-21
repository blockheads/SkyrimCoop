#pragma once

struct ActiveEffect;
struct BGSLoadFormBuffer;

struct InventoryEntry;

// A loaded clip set (BSTArray of the files' handles).
struct AnimationClipSet
{
    void* data;
    uint32_t capacity;
    uint32_t pad0C;
    uint32_t size; // 10
};
static_assert(offsetof(AnimationClipSet, size) == 0x10);

// A process's clip sets, 0x20 apiece, each a load under way then the set kept (40055): 3 the action's (39625), 4 an
// instant interaction's (39291).
struct AnimationSets
{
    uint8_t pad0[0x60];
    void* actionLoad; // 60, the action's load under way
    uint8_t pad68[0x80 - 0x68];
    void* instantLoad; // 80, an instant interaction's load under way
};
static_assert(offsetof(AnimationSets, actionLoad) == 0x60);
static_assert(offsetof(AnimationSets, instantLoad) == 0x80);

// A commander's summon or raised corpse with the effect keeping it (AIProcess::AddCommandedActor).
struct CommandedActor
{
    uint32_t handle; // the actor's
    uint32_t pad04;
    ActiveEffect* pEffect; // 08
};
static_assert(sizeof(CommandedActor) == 0x10);

struct MiddleProcess
{
    // void SaveActiveEffects()
    void LoadActiveEffects(BGSLoadFormBuffer* apLoadGameBuffer);

    // 0xB0 - pitch
    uint8_t pad0[0xB8];
    float direction; // B8
    uint8_t padBC[0x100 - 0xBC];
    GameArray<CommandedActor> commandedActors; // 100, its limit kept by AddCommandedActor
    uint8_t pad118[0x1A0 - 0x118];
    GameList<ActiveEffect>* ActiveEffects;
    uint8_t pad1A8[0x1B8 - 0x1A8];
    AnimationSets* animationSets; // 1B8
    uint8_t pad1C0[0x1E8 - 0x1C0];
    BSRecursiveLock animationLock; // 1E8, guards the sets
    uint8_t pad1F0[0x208 - 0x1F0];
    uint32_t occupiedFurniture; // 208 ObjectRefHandle (CommonLib MiddleHighProcessData)
    uint8_t pad20C[0x218 - 0x20C];
    BSPointerHandle<TESObjectREFR> commandingActor;
    uint8_t pad21C[0x220 - 0x21C];
    InventoryEntry* leftEquippedObject;
    uint8_t pad228[0x230 - 0x228];
    void* instantClipsLoaded; // 230, the callback of an instant interaction's load under way (39291, assigned by 39731)
    uint8_t pad238[0x250 - 0x238];
    struct bhkCharacterController* charController; // 250, NiPointer (CommonLib MiddleHighProcessData)
    uint8_t pad258[0x260 - 0x258];
    InventoryEntry* rightEquippedObject;
    InventoryEntry* ammoEquippedObject; // could be more than just ammo
    uint8_t pad270[0x2F8 - 0x270];
    uint32_t deferredKill; // 2F8: 0 none, 1 held, above 1 kills counted (DeferKill 38617, ended by 40036)
    uint8_t pad2FC[0x332 - 0x2FC];
    bool inDeferredKill; // 332, the scripted flag (CommonLib inDeferredKill)
    // 0xB8 - direction
    //
    // 0x326 - bool lookat
};

static_assert(offsetof(MiddleProcess, direction) == 0xB8);
static_assert(offsetof(MiddleProcess, commandedActors) == 0x100);
static_assert(offsetof(MiddleProcess, animationSets) == 0x1B8);
static_assert(offsetof(MiddleProcess, animationLock) == 0x1E8);
static_assert(offsetof(MiddleProcess, occupiedFurniture) == 0x208);
static_assert(offsetof(MiddleProcess, leftEquippedObject) == 0x220);
static_assert(offsetof(MiddleProcess, instantClipsLoaded) == 0x230);
static_assert(offsetof(MiddleProcess, charController) == 0x250);
static_assert(offsetof(MiddleProcess, rightEquippedObject) == 0x260);
static_assert(offsetof(MiddleProcess, deferredKill) == 0x2F8);
static_assert(offsetof(MiddleProcess, inDeferredKill) == 0x332);
