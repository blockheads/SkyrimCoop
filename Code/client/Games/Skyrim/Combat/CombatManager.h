#pragma once

// CommonLibSSE-NG CombatManager: every combat group of this game. Only handed to the engine's group primitives.
struct CombatManager
{
    [[nodiscard]] static CombatManager* Get() noexcept; // 405246
};
