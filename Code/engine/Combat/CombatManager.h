#pragma once

struct Actor;

// CommonLibSSE-NG CombatManager: every combat group of this game. Only handed to the engine's group primitives.
struct CombatManager
{
    [[nodiscard]] static CombatManager* Get() noexcept; // 405246
    // 46877: an actor's lowest stealth points over its combat groups (the eye in combat, 40593).
    [[nodiscard]] float GetLowestStealthPoints(Actor* apActor) noexcept;
};
