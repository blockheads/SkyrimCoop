#pragma once

#include <Structs/ActorAiState.h>

/**
 * @brief A copy on this game took its owner's AI state (AiStateService, from the copy's record).
 */
struct CopyAiStateEvent
{
    uint32_t SessionId{};
    ActorAiState State{};
};
