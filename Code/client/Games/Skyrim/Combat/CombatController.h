#pragma once

#include <NetImmerse/NiPointer.h>
#include <Actor.h>
#include <Games/Primitives.h>

struct CombatTargetSelector;
struct TESCombatStyle;
struct CombatAreaStandard;
struct CombatGroup;
struct CombatState;
struct CombatInventory;
struct CombatAimController;

struct CombatController
{
    void SetTarget(Actor* apTarget);
    void UpdateTarget();
    // The fighting actor, read as SetTarget (33235) reads it: the cached pointer while handles are held.
    [[nodiscard]] Actor* GetAttacker() const noexcept;

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

static_assert(offsetof(CombatController, attackerHandle) == 0x28);
static_assert(offsetof(CombatController, targetHandle) == 0x2C);
static_assert(offsetof(CombatController, startedCombat) == 0x35);
static_assert(offsetof(CombatController, targetSelectors) == 0xA0);
static_assert(offsetof(CombatController, pActiveTargetSelector) == 0xB8);
static_assert(offsetof(CombatController, handleCount) == 0xC8);
static_assert(offsetof(CombatController, pCachedAttacker) == 0xD0);
static_assert(sizeof(CombatController) == 0xE0);
