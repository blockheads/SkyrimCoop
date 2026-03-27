#pragma once

struct CombatTargetSelector;
struct CombatGroup;
struct CombatState;
struct CombatInventory;
struct CombatAimController;
struct CombatTargetSelector;

struct CombatController
{
    void SetTarget(Actor* apTarget);
    void UpdateTarget();

    CombatGroup *pCombatGroup;
    CombatState *pState;
    CombatInventory *pInventory;
    void *pCombatBlackboard;
    void *pBehaviorController;
    uint32_t attackerHandle;
    uint32_t targetHandle;
    uint32_t previousTargetHandle;
    uint8_t unk34;
    bool startedCombat;
    uint8_t unk36;
    uint8_t unk37;
    TESCombatStyle *pCombatStyle;
    bool stoppedCombat;
    bool unk41;
    bool ignoringCombat;
    bool inactive;
    float unk44;
    float unk4C;
    GameArray<void*> aimControllers;
    uint64_t aimControllerLock;
    CombatAimController *pCurrentAimController;
    CombatAimController *pPreviousAimController;
    GameArray<void*> areas;
    CombatAreaStandard *pCurrentArea;
    GameArray<CombatTargetSelector*> targetSelectors;
    CombatTargetSelector *pActiveTargetSelector;
    CombatTargetSelector *pPreviousTargetSelector;
    uint32_t handleCount;
    int32_t unkCC;
    NiPointer<Actor> pCachedAttacker;
    NiPointer<Actor> pCachedTarget;
};

SKYRIM_STRUCT_ASSERT(offsetof(CombatController, targetHandle) == 0x2C);
SKYRIM_STRUCT_ASSERT(offsetof(CombatController, startedCombat) == 0x35);
SKYRIM_STRUCT_ASSERT(offsetof(CombatController, targetSelectors) == 0xA0);
SKYRIM_STRUCT_ASSERT(offsetof(CombatController, pActiveTargetSelector) == 0xB8);
SKYRIM_STRUCT_ASSERT(sizeof(CombatController) == 0xE0);
