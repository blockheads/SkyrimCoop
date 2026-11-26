// FakeEngine: this game's player (IPlayerBridge).
#include <core/MenuEvent.h>
#include "FakeEngine.h"
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <mounts/MountEvent.h>
#include <characters/ActorRenamedEvent.h>
#include <core/DifficultyChangeEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <actorstate/DeathActions.h>
#include <travel/SetWaypointEvent.h>
#include <core/PlayerFormChangeEvent.h>
#include <core/World.h>

using namespace PartyRules;

bool FakeEngine::PlayerSitOnMountQuick(uint32_t aMount) noexcept
{
    if (!Actors.contains(aMount))
        return false;
    Mounts[kPlayer] = aMount;
    MountSlots.try_emplace(aMount, kPlayer); // 12074 adds only to an empty slot: the driver keeps it
    if (RefuseQuickSeat)
    {
        EndRide(kPlayer); // 40356 ends a rider not seated: its Detach (41539) empties the slot
        return false;
    }
    // IDLE EnterHorseInstant (condition 631): HorseEnterInstant, which seats it (ApplyActionWrites). No paired clip.
    ActionEvent instant;
    instant.ActionId = 0x13009; // ActionActivate (default object 55)
    instant.EventName = "HorseEnterInstant";
    instant.TargetId = aMount;
    if (!PerformAnimation(kPlayer, instant))
        return false;
    SeatedRiders.insert(kPlayer); // 41530's OnSeated (40353)
    return true;
}

bool FakeEngine::ActivateMount(uint32_t aActivator, uint32_t aMount)
{
    if (!IsActorMount(aMount) || !Actors.contains(aActivator) || IsActorMount(aActivator))
        return false;
    const bool cCopy = Actors.at(aActivator).Copy;
    if (const auto it = Mounts.find(aActivator); it != Mounts.end())
    {
        if (it->second != aMount)
            return true;
        if (cCopy)
            GettingOnOff.insert(aActivator); // a dismount package no copy runs
        else
            StartDismount(aActivator);
        return true;
    }
    if (MountSlots.contains(aMount))
    {
        // IsMountBeingRidden: nothing. Our hook (37864) offers our player's activation as a boarding.
        if (aActivator == kPlayer && !cCopy)
            World::Get().GetRunner().Trigger(BoardEvent{aActivator, aMount});
        return true;
    }
    if (Actors.at(aActivator).InAir)
        return true; // CanRide (38706) refuses: no package, no ride, our hook raises nothing
    if (cCopy)
    {
        GettingOnOff.insert(aActivator); // a mount package no copy runs
        GettingOnOff.insert(aMount);
        return true;
    }
    World::Get().GetRunner().Trigger(MountEvent{aActivator, aMount});
    if (Synced(aMount))
    {
        SendGraphEvent(aMount, "PairEnd"); // the refused pair's own instance ends it
        ++RefusedGetOns;
        MountRetryOf[aActivator] = aMount;
        return true;
    }
    Mounts[aActivator] = aMount;
    MountSlots[aMount] = aActivator;
    if (aActivator == kPlayer)
        Reins[aMount] = aActivator; // OnMountStarted (40725), its package the player's ride
    return true;
}

void FakeEngine::EndRide(uint32_t aRider) noexcept
{
    DismountIn.erase(aRider);
    const auto cMount = Mounts.find(aRider);
    if (cMount == Mounts.end())
        return;
    const uint32_t cMountId = cMount->second;
    Mounts.erase(cMount);
    SeatedRiders.erase(aRider);
    MountSlots.erase(cMountId); // 40355 with null: whichever rider it named
    if (const auto cReins = Reins.find(cMountId); cReins != Reins.end() && cReins->second == aRider)
        Reins.erase(cReins);
    // An owner's exit pair: its instance ends before the Detach, in its frame (32797).
    if (const auto it = Actors.find(aRider); it != Actors.end() && !it->second.Copy && Synced(cMountId) && !Actors.at(cMountId).Copy)
        SendGraphEvent(cMountId, "PairEnd");
    // HookDetachInteraction: any rider's ride ended.
    if (const auto it = Actors.find(aRider); it != Actors.end())
        World::Get().GetRunner().Trigger(DismountEvent{aRider, cMountId, it->second.Copy});
}

void FakeEngine::PlayerTravels(uint32_t aCell, const glm::vec3& acPosition)
{
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
}

void FakeEngine::PlayerTravelsUnannounced(uint32_t aCell, const glm::vec3& acPosition)
{
    m_unannouncedMove = true;
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
    m_unannouncedMove = false;
}

// Resurrect: alive, its pools full (its maximums kept), its death action over, reported as the Resurrect hook reports
// it; placement is the party follower move.
void FakeEngine::RevivePlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    if (!player.Dead)
        return;
    player.Dead = false;
    if (std::exchange(AshDead, false)) // CriticalStage::ShowPlayerBody: the roots as the camera has them
        ShowFirstPersonRoot(CameraFirstPerson);
    player.DyingFrames = player.DeadFrames = 0;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
    {
        player.Values.ActorValuesList[cIndex] = player.Values.ActorMaxValuesList[cIndex];
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{kPlayer, cIndex});
    }
    const auto cAction = std::exchange(player.LatestAnimation, {});
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer, true, true,
        cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
}

void FakeEngine::HoldDeathControls(bool aHold) noexcept
{
    constexpr uint32_t flags = 0x1 | 0x4 | 0x20 | 0x40 | 0x80 | 0x400;
    if (aHold && !DeathControlsHeld)
    {
        DeathControlsBefore = Controls & flags;
        Holds.Take(0xFFFFFFFE, flags);
        Controls &= ~flags;
    }
    else if (!aHold && DeathControlsHeld)
        Controls |= Holds.GiveBack(0xFFFFFFFE, flags) & DeathControlsBefore;
    DeathControlsHeld = aHold;
}

void FakeEngine::PlacesMarker(const glm::vec3& acPosition, uint32_t aWorldSpace)
{
    Vector3_NetQuantize position;
    position.x = acPosition.x;
    position.y = acPosition.y;
    position.z = acPosition.z;
    World::Get().GetRunner().Trigger(SetWaypointEvent(position, aWorldSpace));
    Waypoint = Marker{acPosition, aWorldSpace};
}

void FakeEngine::ChoosesDifficulty(uint32_t aDifficulty)
{
    Difficulty = aDifficulty;
    World::Get().GetRunner().Trigger(DifficultyChangeEvent{aDifficulty});
}

void FakeEngine::PlayerSwitchesRace(uint32_t aRace)
{
    Npcs[Actors.at(kPlayer).Base].Race = aRace;
    Player.Base = std::format("race {:X}", aRace);
    Player.BaseFlags |= 0x2000000; // CHANGE_NPC_RACE
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerFormChangeEvent{});
}

void FakeEngine::PlayerUsesRaceMenu(const std::function<void()>& acChanges)
{
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", true});
    acChanges();
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", false});
}

void FakeEngine::PlayerNames(const std::string& acName)
{
    Player.Name = acName;
    Player.BaseFlags |= 0x20;
}

void FakeEngine::AliasNames(uint32_t aFormId, const std::string& acName)
{
    Actors.at(aFormId).DisplayName = acName;
    Actors.at(aFormId).CustomName = false;
    Actors.at(aFormId).NameEmptied = false;
    World::Get().GetRunner().Trigger(ActorRenamedEvent{aFormId, GetActorDisplayName(aFormId)});
}

bool FakeEngine::OpenFollowerInventory(uint32_t aNpcFormId) noexcept
{
    if (FollowerTradeNpc || !Actors.contains(aNpcFormId))
        return false;
    FollowerTradeNpc = aNpcFormId;
    World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    return true;
}

void FakeEngine::EndScene(uint32_t aActor)
{
    // TESSceneEvent can be delivered before the final actor scene field is cleared.
    World::Get().GetDispatcher().trigger(AiStateChangeEvent{aActor});
    Actors.at(aActor).Ai.SceneId = 0;
}
