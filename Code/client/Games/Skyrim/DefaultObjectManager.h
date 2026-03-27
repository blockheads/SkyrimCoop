#pragma once

struct BGSAction;

struct DefaultObjectManager
{
    static DefaultObjectManager& Get();

    uint8_t padB8[0xB8];
    TESForm* leftEquipSlot;
    TESForm* rightEquipSlot; // also ammo slot?
    TESForm* eitherEquipSlot;
    TESForm* voiceEquipSlot;
    TESForm* potionEquipSlot;
    uint8_t pad0[0x220 - 0xE0];
    BGSAction* someAction; // 220
    uint8_t pad228[0xBC0 - 0x228];
    bool isSomeActionReady; // BC0
};

SKYRIM_STRUCT_ASSERT(offsetof(DefaultObjectManager, leftEquipSlot) == 0xB8);
SKYRIM_STRUCT_ASSERT(offsetof(DefaultObjectManager, rightEquipSlot) == 0xC0);
SKYRIM_STRUCT_ASSERT(offsetof(DefaultObjectManager, potionEquipSlot) == 0xD8);
SKYRIM_STRUCT_ASSERT(offsetof(DefaultObjectManager, someAction) == 0x220);
SKYRIM_STRUCT_ASSERT(offsetof(DefaultObjectManager, isSomeActionReady) == 0xBC0);
