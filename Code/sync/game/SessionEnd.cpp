#include <game/SessionEnd.h>

#include <join/IJoinBridge.h>
#include <game/HostService.h>
#include <join/JoinService.h>
#include <core/SessionService.h>
#include <join/SteamService.h>
#include <core/TransportService.h>
#include <core/World.h>

void WireSessionEnd(World& aWorld) noexcept
{
    aWorld.ctx().at<SessionService>().SetEnding({
        .IsJoining = [&aWorld] { return aWorld.ctx().at<JoinService>().IsJoining(); },
        .Finish =
            [&aWorld](const std::string& acReason, bool aReturnToMenu) {
                auto& host = aWorld.GetHostService();
                auto& join = aWorld.ctx().at<JoinService>();
                // The final character record needs the Steam identity until the transport closes.
                if (host.IsHosting())
                    host.AnnounceEnd();
                aWorld.GetTransport().Close();
                host.StopHosting();
                auto& steam = aWorld.ctx().at<SteamService>();
                steam.EndRejoin();
                steam.Leave();
                join.Cancel(acReason);
                auto& bridge = aWorld.ctx().at<IJoinBridge&>();
                if (aReturnToMenu && !bridge.IsAtMainMenu())
                    bridge.ReturnToMainMenu();
            },
    });
}
