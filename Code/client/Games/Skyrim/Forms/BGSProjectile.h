#pragma once

#include "TESBoundObject.h"

// A projectile's base form (CommonLibSSE-NG BGSProjectile; DATA at +0x80).
struct BGSProjectile : TESBoundObject
{
    uint8_t pad30[0x80 - sizeof(TESBoundObject)];
    uint16_t flags; // 80
    uint16_t types; // 82
    uint8_t pad84[0xB0 - 0x84];
    // What it makes on impact: Projectile::Launch (44108, 0x1407F9A4D) copies it to the projectile (+0x158).
    TESForm* pExplosion; // B0
};
static_assert(offsetof(BGSProjectile, flags) == 0x80);
static_assert(offsetof(BGSProjectile, pExplosion) == 0xB0);
