#include <Services/SessionEnd.h>

#include <Bridges/IJoinBridge.h>
#include <Services/HostService.h>
#include <Services/JoinService.h>
#include <Services/SessionService.h>
#include <Services/SteamService.h>
#include <Services/TransportService.h>
#include <World.h>

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
