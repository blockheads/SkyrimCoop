#pragma once

#include <Structs/ActionEvent.h>

#include <chrono>

struct LocalComponent
{
    LocalComponent(uint32_t aId) noexcept
        : Id(aId)
    {
    }

    uint32_t Id;
    ActionEvent CurrentAction;
    std::chrono::steady_clock::time_point LastSent; // its last movement snapshot (CharacterService::RunLocalUpdates)
};
