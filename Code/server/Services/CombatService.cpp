#include <Services/CombatService.h>
#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/NotifyProjectileLaunch.h>

namespace Server
{

CombatService::CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_projectileLaunchConnection = aDispatcher.sink<PacketEvent<ProjectileLaunchRequest>>().connect<&CombatService::OnProjectileLaunchRequest>(this);
}

void CombatService::OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) const noexcept
{
    auto& packet = acMessage.Packet;

    NotifyProjectileLaunch notify{};

    notify.ShooterID = packet.ShooterID;

    notify.OriginX = packet.OriginX;
    notify.OriginY = packet.OriginY;
    notify.OriginZ = packet.OriginZ;

    notify.ProjectileBaseID = packet.ProjectileBaseID;
    notify.WeaponID = packet.WeaponID;
    notify.AmmoID = packet.AmmoID;

    notify.ZAngle = packet.ZAngle;
    notify.XAngle = packet.XAngle;
    notify.YAngle = packet.YAngle;

    notify.ParentCellID = packet.ParentCellID;

    notify.SpellID = packet.SpellID;
    notify.CastingSource = packet.CastingSource;

    notify.Area = packet.Area;
    notify.Power = packet.Power;
    notify.Scale = packet.Scale;

    notify.AlwaysHit = packet.AlwaysHit;
    notify.NoDamageOutsideCombat = packet.NoDamageOutsideCombat;
    notify.AutoAim = packet.AutoAim;
    notify.DeferInitialization = packet.DeferInitialization;
    notify.ForceConeOfFire = packet.ForceConeOfFire;

    notify.UnkBool1 = packet.UnkBool1;
    notify.UnkBool2 = packet.UnkBool2;

    GameServer::Get()->SendToPlayers(notify, acMessage.GetSender());
}

} // namespace Server
