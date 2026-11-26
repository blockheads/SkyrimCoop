#pragma once

#include <Messages/Payloads.h>

#include <array>
#include <cstdint>

struct Actor;
struct TESObjectREFR;

// What ProcessTrapHit (56212) gives its target: TESObjectREFR vfunc 0x58 (Actor::DoTrap1 37509).
struct alignas(16) TrapData
{
    uint32_t trapHandle;   // 00
    float damage;          // 04
    float damageBonus;     // 08, 0: added to damage (44000)
    uint32_t unk0C;        // 0C
    float push;            // 10
    uint32_t unk14;        // 14
    float stagger;         // 18
    uint8_t unk1C;         // 1C
    uint8_t pad1D[3];      // 1D
    float velocity[4];     // 20, Havok units
    float position[4];     // 30
    uint32_t material;     // 40
    uint32_t pad44[3];     // 44
};
static_assert(offsetof(TrapData, damage) == 0x04 && offsetof(TrapData, push) == 0x10 && offsetof(TrapData, stagger) == 0x18 &&
              offsetof(TrapData, velocity) == 0x20 && offsetof(TrapData, position) == 0x30 && offsetof(TrapData, material) == 0x40 &&
              sizeof(TrapData) == 0x50);

namespace Traps
{
// This game's actor takes the hit the host's script gave its copy there (Actor::DoTrap1 37509).
void TakeHit(Actor& aActor, const TESObjectREFR* apTrap, const Payloads::TrapHit& acHit) noexcept;
// On the host: a guest's contact with a trap, raised for its copy as the engine's own (TESTrapHitEvent).
void RaiseContact(TESObjectREFR& aTrap, Actor& aCopy, const Payloads::TrapContact& acContact) noexcept;
} // namespace Traps
