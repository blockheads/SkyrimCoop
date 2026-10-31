#pragma once

#include <cstdint>

// A guest's quest is started by the host's state (GuestEngine::ApplyQuest), not by the engine's start, which reads the
// quest's dialogue lines (TESQuest 25086; docs/re/systems/dialogue.md). Checked by ClientTests.
namespace QuestStarts
{
// TESQuest flags: running, or waiting to start (StageWait).
inline constexpr uint16_t cStartedFlags = 0x01 | 0x80;

// The host's state starts the quest here: from stopped to started, its lines must be read in.
[[nodiscard]] inline bool LoadsDialogue(uint16_t aWasFlags, uint16_t aNowFlags) noexcept
{
    return !(aWasFlags & cStartedFlags) && (aNowFlags & cStartedFlags);
}
} // namespace QuestStarts
