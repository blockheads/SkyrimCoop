#pragma once
#include <cstdint>

// Applied health damage, after resistance; emitted by Actor::HandleHealthDamage (38606).
struct ActorDamagedEvent
{
    uint32_t VictimId{};
    uint32_t AttackerId{};
    float Damage{};
};
