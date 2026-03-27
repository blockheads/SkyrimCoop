#pragma once

#include <AI/AITimer.h>

struct CombatController;

struct CombatTargetSelector
{
    virtual ~CombatTargetSelector();
    virtual void Unk1();
    virtual void Unk2();
    virtual void Unk3();
    virtual void Unk4();
    virtual void Update();
    virtual BSPointerHandle<Actor> SelectTarget();

    void *vftable_NiRefObject_8;
    CombatController* pCombatController;
    BSPointerHandle<Actor> hTarget;
    uint32_t ePriority;
    uint32_t flags;
    uint8_t pad24[4];
};
SKYRIM_STRUCT_ASSERT(offsetof(CombatTargetSelector, ePriority) == 0x1C);
SKYRIM_STRUCT_ASSERT(sizeof(CombatTargetSelector) == 0x28);

struct CombatTargetSelectorStandard : public CombatTargetSelector
{
    AITimer updateTimer;
};
SKYRIM_STRUCT_ASSERT(sizeof(CombatTargetSelectorStandard) == 0x30);
