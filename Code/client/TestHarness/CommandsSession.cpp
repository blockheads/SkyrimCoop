#include "Commands.h"
#include "Status.h"
#include "EngineCalls.h"

#include <World.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>
#include <Services/SteamService.h>
#include <Bridges/IPlayerBridge.h>

// The session: leaving, kicking, and swapping in a rebuilt plugin. Hosting and joining are driven by
// session.txt (Session.h).
namespace TestHarness
{
namespace
{
// {"cmd": "disconnect"}: leaves the session like a player quitting (join again with a new session
// request). The host is the session: when it leaves, the session ends for everyone.
nlohmann::json Disconnect(Harness&, const nlohmann::json&)
{
    auto& world = World::Get();
    if (world.GetHostService().IsHosting())
        world.GetHostService().StopHosting();
    else
        world.GetTransport().Close();
    return Ok();
}

// {"cmd": "kick", "player": id}: the host removes a player from the session.
nlohmann::json Kick(Harness&, const nlohmann::json& acRequest)
{
    auto& host = World::Get().GetHostService();
    if (!host.IsHosting())
        return Error("not hosting");
    host.KickPlayer(acRequest.value("player", 0u));
    return Ok();
}

// {"cmd": "difficulty", "value": 0-5}: sets the player's difficulty, as the settings menu does;
// answers with the difficulty right after (before the session's own update can change it).
nlohmann::json Difficulty(Harness&, const nlohmann::json& acRequest)
{
    EngineCalls::ChooseDifficulty(acRequest.value("value", 2u));
    return {{"ok", true}, {"difficulty", World::Get().GetPlayerBridge().GetDifficulty()}};
}

// {"cmd": "reload"}: the loader swaps in the current build next frame (Code/hotreload/Loader.cpp).
nlohmann::json Reload(Harness&, const nlohmann::json&)
{
    const HMODULE cLoader = GetModuleHandleW(L"SkyrimCoopLoader.dll");
    const auto pRequest = cLoader ? reinterpret_cast<void (*)()>(GetProcAddress(cLoader, "SkyrimCoopLoader_RequestReload")) : nullptr;
    if (!pRequest)
        return Error("not loaded by SkyrimCoopLoader");
    pRequest();
    return {{"ok", true}, {"reload", ReloadStatus()}};
}

// {"cmd": "steam_host"}: makes a Steam lobby and listens, as the Host button will.
nlohmann::json SteamHost(Harness&, const nlohmann::json&)
{
    return World::Get().ctx().at<SteamService>().Host() ? Ok() : Error("Steam not ready, or a session under way");
}

// {"cmd": "steam_invite", "friend": "<SteamID64>"}: the host invites a friend to its lobby, as the overlay's invite does.
nlohmann::json SteamInvite(Harness&, const nlohmann::json& acRequest)
{
    const uint64_t cFriend = std::stoull(acRequest.value("friend", std::string("0")));
    return World::Get().ctx().at<SteamService>().Invite(cFriend) ? Ok() : Error("not hosting a lobby, or Steam refused");
}

// {"cmd": "steam_leave"}: leaves the Steam session (the host's ends it for everyone in it).
nlohmann::json SteamLeave(Harness&, const nlohmann::json&)
{
    World::Get().ctx().at<SteamService>().Leave();
    return Ok();
}

constexpr Command cCommands[] = {
    {"steam_host", false, &SteamHost},
    {"steam_leave", false, &SteamLeave},
    {"steam_invite", false, &SteamInvite},
    {"disconnect", false, &Disconnect},
    {"kick", false, &Kick},
    {"difficulty", false, &Difficulty},
    {"reload", false, &Reload},
};
} // namespace

std::span<const Command> SessionCommands()
{
    return cCommands;
}
} // namespace TestHarness
