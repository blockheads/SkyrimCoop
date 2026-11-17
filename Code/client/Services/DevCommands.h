#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The dev chat commands (/heal, /spawn, ...): parsed here, run as console lines (docs/design/dev-commands.md).
// Pure: every game's DevToolsService and the host's HostDevTools share it.
namespace DevCommands
{
enum class Kind : uint8_t
{
    Heal,
    God,
    Speed,
    Gravity,
    Shouts,
    Gear,
    LootBox,
    KillMove,
    Reset,
    Spawn,
    KillAll,
    Weather,
};

struct Command
{
    Kind What{};
    float Value{1.f};            // speed, gravity
    uint32_t Count{1};           // spawn, lootbox
    uint32_t FormId{};           // spawn's NPC or leveled list, weather; gear's set (0 daedric, 1 glass)
    std::optional<bool> On;      // god: on, off, or toggle
    std::optional<uint8_t> Odds; // kill move percent; none: the game's own
    std::string Text;            // the line as typed, for the chat
};

struct Parsed
{
    std::optional<Command> Run;
    std::string Error; // a usage line when Run is empty
};

// The line names a dev command (its first word), valid or not.
[[nodiscard]] bool IsDevCommand(std::string_view aLine) noexcept;
[[nodiscard]] Parsed Parse(std::string_view aLine) noexcept;
[[nodiscard]] std::string Help() noexcept;

// A world command is the host's alone; the others run on every game, each on its own player.
[[nodiscard]] bool IsWorld(Kind aKind) noexcept;

// What a game's own player has on from earlier commands: /reset undoes it, /god toggles it.
struct PlayerState
{
    bool God{};
    bool Speed{};
    bool Gravity{};
};

// An actor as the console names it: "player", or its reference id when that starts with a letter (a copy's FF...;
// docs/re/systems/console.md); none for 0 or an id the console takes as a number.
[[nodiscard]] std::optional<std::string> ConsoleName(uint32_t aFormId, uint32_t aPlayerFormId) noexcept;

// Each game's console lines on its own player; updates aState.
[[nodiscard]] std::vector<std::string> PlayerLines(const Command& acCommand, PlayerState& aState) noexcept;
// The host's console lines for a world command; aAt is the typer's character as the console names it.
[[nodiscard]] std::vector<std::string> WorldLines(const Command& acCommand, std::string_view aAt) noexcept;

// The kill move conditions with odds set (/killmove): ShouldAttackKill 21744 and IsLastHostileActor 21743.
// aRoll is 0-99. Without odds the game's own answers stand.
[[nodiscard]] bool ShouldAttackKill(bool aFatal, std::optional<uint8_t> aOdds, uint32_t aRoll) noexcept;
[[nodiscard]] bool IsLastHostileActor(bool aLast, std::optional<uint8_t> aOdds) noexcept;
} // namespace DevCommands
