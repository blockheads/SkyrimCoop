#pragma once

#include <Bridges/IDevToolsBridge.h>

// The dev commands in Skyrim: the console, the actors loaded here, the kill move conditions' odds.
struct DevToolsBridge final : IDevToolsBridge
{
    void RunConsole(const std::string& acLine) noexcept override;
    uint32_t KillHostiles() noexcept override;
    void SetKillMoveOdds(std::optional<uint8_t> aOdds) noexcept override;
};
