#pragma once

#include <tools/IDevToolsBridge.h>

// The dev commands in Skyrim: the console, the actors loaded here, the kill move conditions' odds.
struct DevToolsBridge final : IDevToolsBridge
{
    void RunConsole(const std::string& acLine) noexcept override;
    uint32_t KillHostiles() noexcept override;
    uint32_t SpawnPartyHorses(uint32_t aAtFormId, uint32_t aCount) noexcept override;
    bool IsGodMode() noexcept override;
    void SetGodMode(bool aOn) noexcept override;
    void SetKillMoveOdds(std::optional<uint8_t> aOdds) noexcept override;
};
