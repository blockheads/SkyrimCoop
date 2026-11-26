#pragma once

// The story manager: picks the quests a story event starts (docs/re/systems/story-manager.md).
struct BGSStoryManager
{
    // Off: every story event is handled with no quest started (ProcessEvent 32372 is skipped).
    static void SetPicksQuests(bool aPicks) noexcept;
};
