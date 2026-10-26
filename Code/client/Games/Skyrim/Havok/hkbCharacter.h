#pragma once

struct CharacterContext;

struct hkbRagdollDriver;

struct hkbCharacter
{
    virtual ~hkbCharacter(){};

    uint8_t pad8[0x30 - 0x8];
    hkbRagdollDriver* ragdollDriver; // 30 (CommonLib-NG, Precision)
    uint8_t pad38[0xA0 - 0x38];
    CharacterContext* characterContext;
    uint8_t pad88[0xA0 - 0x88];
};

static_assert(offsetof(hkbCharacter, ragdollDriver) == 0x30);
