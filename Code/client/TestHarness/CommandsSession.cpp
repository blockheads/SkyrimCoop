#include <TiltedOnlinePCH.h>
#include "Commands.h"
#include "Status.h"

#include <World.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>

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

constexpr Command cCommands[] = {
    {"disconnect", false, &Disconnect},
    {"kick", false, &Kick},
    {"reload", false, &Reload},
};
} // namespace

std::span<const Command> SessionCommands()
{
    return cCommands;
}
} // namespace TestHarness
