#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// What the host's rules read and change in its game: dev commands run in the host's game. A part of HostGame.
struct HostDevToolsGame
{
    // Dev commands (docs/design/dev-commands.md): a console line run in the host's game; a session's character as the
    // console names it ("player", or its copy's reference id), none when not loaded; the hostile actors killed.
    std::function<void(const std::string& acLine)> RunConsole;
    std::function<std::optional<std::string>(uint32_t aSessionId)> ConsoleNameOf;
    std::function<uint32_t()> KillHostiles;
    // /spawn horse: that many party horses beside a session's character in the host's game; how many were placed.
    std::function<uint32_t(uint32_t aSessionId, uint32_t aCount)> SpawnPartyHorses;
};
