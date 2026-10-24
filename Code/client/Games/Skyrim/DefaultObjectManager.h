#pragma once

#include <Forms/TESForm.h>

struct BGSAction;

struct DefaultObjectManager
{
    static DefaultObjectManager& Get();
    enum Object : uint32_t
    {
        kWerewolfAvailablePerks = 320,
        kVampireAvailablePerks = 321,
    };
    // The engine's GetDefaultObject (11436), including its initialized check.
    static TESForm* GetObject(Object aObject) noexcept;

    uint8_t padB8[0xB8];
    TESForm* leftEquipSlot;
    TESForm* rightEquipSlot; // also ammo slot?
    TESForm* eitherEquipSlot;
    TESForm* voiceEquipSlot;
    TESForm* potionEquipSlot;
    uint8_t pad0[0x220 - 0xE0];
    BGSAction* someAction; // 220
    uint8_t pad228[0x310 - 0x228];
    BGSAction* actionDeath;     // 310, CommonLibSSE-NG kActionDeath (94): what KillImpl performs
    BGSAction* actionDeathWait; // 318, kActionDeathWait (95)
    uint8_t pad320[0xBC0 - 0x320];
    bool isSomeActionReady; // BC0
};

static_assert(offsetof(DefaultObjectManager, leftEquipSlot) == 0xB8);
static_assert(offsetof(DefaultObjectManager, rightEquipSlot) == 0xC0);
static_assert(offsetof(DefaultObjectManager, potionEquipSlot) == 0xD8);
static_assert(offsetof(DefaultObjectManager, someAction) == 0x220);
static_assert(offsetof(DefaultObjectManager, actionDeath) == 0x310);
static_assert(offsetof(DefaultObjectManager, isSomeActionReady) == 0xBC0);
