#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The quests one per player (docs/design/system-quests.md): each game runs its own instance for its own player.
// Learned when this game's player's own script calls into one (GuestScripts' call hook), from the other games
// (SystemQuestService) and from the save (CoSave).
namespace SystemQuests
{
// aQuest is one per player from now on. True when it is new here; aTell: the other games hear of it.
bool Learn(uint32_t aQuest, bool aTell) noexcept;
[[nodiscard]] bool Any() noexcept;
[[nodiscard]] bool Is(uint32_t aQuest) noexcept;
// A global a system quest's scripts hold (a property), not the calendar's: each game's own value.
[[nodiscard]] bool IsGlobal(uint32_t aGlobal) noexcept;
[[nodiscard]] std::vector<uint32_t> All() noexcept;
// A new game, or a load starting.
void Clear() noexcept;

// A timer native (ScriptTags' table) called on aQuest's script: its registration, kept for this player's record.
void TimerCalled(uint32_t aQuest, std::string_view aNative, float aInterval) noexcept;

// This game's instances, for its player's record (PlayerRecord::Extras, SystemQuestState).
[[nodiscard]] std::string ReadState() noexcept;
// On a guest after the join's load: the instances the record keeps put back, every other one reset (the save's is
// the host's player's).
void ApplyState(std::string_view aState) noexcept;
} // namespace SystemQuests
