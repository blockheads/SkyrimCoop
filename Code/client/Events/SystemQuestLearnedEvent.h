#pragma once

#include <cstdint>

// This game's player's own script called into a quest that is now one per player (SystemQuests): the other games
// hear of it (docs/design/system-quests.md).
struct SystemQuestLearnedEvent
{
    uint32_t Quest{};
};
