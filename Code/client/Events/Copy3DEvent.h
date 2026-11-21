#pragma once

#include <cstdint>

// A copy this game spawned has its 3D now (CharacterService's WaitingFor3D): what shows on it can play. The engine
// raises no TESObjectLoadedEvent for it (measured 2026-11-21, summons.md).
struct Copy3DEvent
{
    uint32_t FormId{};
};
