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
    uint64_t LastSentRound{}; // the send round of its last movement snapshot (CharacterService::RunLocalUpdates)
};
