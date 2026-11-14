#pragma once

#include <Games/Primitives.h>

struct TESForm;

// A combat controller's aim (CommonLib-NG CombatAimController; docs/re/systems/combat-aim.md).
// TESObjectWEAP::Fire (18102) takes an NPC's arrow direction from P through CombatController::GetAimPoint.
struct CombatAimController
{
    void* pVtable;
    uint64_t refCount;
    TESForm* pSource;  // the weapon, for a projectile aim (Fire matches it)
    NiPoint3 aimPoint; // P: solved by CombatProjectileAimController::UpdateAimSolution (44396)
    uint32_t unk24;
    void* pCombatController;
    uint32_t targetHandle;
    uint32_t priority1;
    uint32_t priority2;
    uint32_t flags; // 0x1 solved, 0x2 updating, 0x40 held until fired
    uint64_t timer;
};
static_assert(offsetof(CombatAimController, pSource) == 0x10);
static_assert(offsetof(CombatAimController, aimPoint) == 0x18);
static_assert(offsetof(CombatAimController, targetHandle) == 0x30);
static_assert(offsetof(CombatAimController, flags) == 0x3C);
static_assert(sizeof(CombatAimController) == 0x48);

// The bow's and crossbow's aim (VTABLE 210081).
struct CombatProjectileAimController : CombatAimController
{
    void* pProjectile;
    float projectileSpeed; // time of flight = distance / speed when the arc has no gravity
    float blastRadius;
    uint8_t unk58[0x98 - 0x58];
    float launchDelay; // added to the time of flight
    float unk9C;
    float accuracy; // scales the random aim offset
};
static_assert(offsetof(CombatProjectileAimController, projectileSpeed) == 0x50);
static_assert(offsetof(CombatProjectileAimController, launchDelay) == 0x98);
static_assert(offsetof(CombatProjectileAimController, accuracy) == 0xA0);
