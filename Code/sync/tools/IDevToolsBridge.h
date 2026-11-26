#pragma once

#include <cstdint>
#include <optional>
#include <string>

// What the dev commands do in this game's engine (docs/design/dev-commands.md).
class IDevToolsBridge
{
  public:
    virtual ~IDevToolsBridge() = default;
    // Queues a console line; it runs later this frame (Console::ExecuteCommand 442672).
    virtual void RunConsole(const std::string& acLine) noexcept = 0;
    // Kills the loaded actors hostile to this game's player, players and their copies spared; how many.
    virtual uint32_t KillHostiles() noexcept = 0;
    // aCount saddled horses (109E3D) placed beside that actor, each the party's as a stable sells one
    // (PlayerHorseFaction rank 1, owned by PlayerFaction); how many were placed.
    virtual uint32_t SpawnPartyHorses(uint32_t aAtFormId, uint32_t aCount) noexcept = 0;
    // God mode, the console's tgm (IsInGodMode 40501, PlayerCharacter::SetGodMode 40500).
    virtual bool IsGodMode() noexcept = 0;
    virtual void SetGodMode(bool aOn) noexcept = 0;
    // The kill move odds in percent (/killmove); none: the game's own rule.
    virtual void SetKillMoveOdds(std::optional<uint8_t> aOdds) noexcept = 0;
};
