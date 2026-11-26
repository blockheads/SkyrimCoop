#pragma once

#include <cstdint>
#include <string>
#include <vector>

// An option of the party's list as a menu shows it (docs/design/shared-dialogue.md): its text, then who can say it
// when not everyone can ("I have your claw. [Lydia]"). No engine.
[[nodiscard]] std::string PartyOptionText(const std::string& acText, const std::vector<std::string>& acNames);
// What the chat tells a player who pressed an option it cannot say (only it reads it).
[[nodiscard]] std::string CannotSayText(const std::string& acPlayerName);
// Whether that player can say an option: who can say it (none: everyone in the conversation).
[[nodiscard]] bool CanSay(const std::vector<uint32_t>& acPlayers, uint32_t aPlayerId);
