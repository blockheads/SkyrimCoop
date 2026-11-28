#include "Party.h"
#include <animation/GraphStream.h>

#include <mounts/DismountEvent.h>
#include <mounts/MountEvent.h>
#include <animation/ReplayState.h>
#include <core/SessionIds.h>
#include <actorstate/AiStateChangeEvent.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>

namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kHorse = 0x109E3D;

bool Owns(World& aGame, uint32_t aForm)
{
    for (const auto cEntity : aGame.view<FormIdComponent, LocalComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aForm)
            return true;
    return false;
}
} // namespace

SCENARIO("A mounted guest owns the horse until dismount or disconnect, then the host takes it back", "[mounts]")
{
    Party party(2, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("all three players and their copies are present");
    auto& host = Party::Engine(party.Host());
    auto& rider = Party::Engine(party.Guest());
    auto& watcher = Party::Engine(party.Guest(1));
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return rider.CopiesOf(kHorse).size() == 1 && watcher.CopiesOf(kHorse).size() == 1; },
                "both guests have the horse");
    const uint32_t cHorse = rider.CopiesOf(kHorse)[0];
    party.As(party.Guest(), [&] {
        rider.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return Owns(party.Guest(), cHorse) && !Owns(party.Host(), horse) && !watcher.Mounts.empty(); },
                "the rider owns the horse and the observer's rider copy mounts it");
    CHECK(host.IsActorCopy(horse));
    CHECK_FALSE(rider.IsActorCopy(cHorse));

    WHEN("the guest dismounts")
    {
        party.As(party.Guest(), [&] { party.Guest().GetRunner().Trigger(DismountEvent{FakeEngine::kPlayer}); });
        party.Until([&] { return Owns(party.Host(), horse) && !Owns(party.Guest(), cHorse); },
                    "the host owns the horse again");
        CHECK_FALSE(host.IsActorCopy(horse));
        CHECK_FALSE(host.Actors.at(horse).Persistent);
        CHECK(rider.IsActorCopy(cHorse));
    }
    WHEN("the host travels while the guest rides")
    {
        constexpr uint32_t cDestination = 0x9C5E1;
        party.As(party.Host(), [&] { host.PlayerTravels(cDestination, {100.f, 200.f, 0.f}); });
        party.Until([&] { return rider.GetPlayerParentCellId() == cDestination; }, "the rider follows");
        party.Until([&] { return rider.Actors.contains(cHorse) && rider.Actors.at(cHorse).Cell == cDestination; },
                    "the owned horse travels with its rider");
        CHECK_FALSE(rider.IsPlayerMounted());
        party.Until([&] { return Owns(party.Host(), horse); },
                    "native travel's dismount returns the travelled horse to the host");
        REQUIRE(host.Actors.contains(horse));
        CHECK(host.Actors.at(horse).Cell == cDestination);
        party.Agree("the returned horse and its copies agree at the destination");
    }
    WHEN("the guest disconnects while riding")
    {
        // A lost disconnect packet takes ENet's 30 s timeout on a conditioned line.
        party.Leave(party.Guest());
        party.Until([&] { return Owns(party.Host(), horse); }, "the disconnected rider's horse returns to the host", {},
                    nullptr, std::chrono::seconds{party.Conditioned() ? 45 : 2});
        CHECK(host.Actors.contains(horse));
        CHECK(watcher.CopiesOf(kHorse).size() == 1);
    }
}

SCENARIO("A horse's empty package is published when its rider becomes its owner", "[mounts][mount-package]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    auto& host = Party::Engine(party.Host());
    auto& rider = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] {
        horse = host.LoadNpc(kHorse, kCell);
        host.Actors.at(horse).Ai.PackageId = 0x109AB2;
    });
    party.Until([&] { return rider.CopiesOf(kHorse).size() == 1; }, "the rider has the host's horse");
    const uint32_t cHorse = rider.CopiesOf(kHorse)[0];
    party.Until([&] { return rider.Actors.at(cHorse).Ai.PackageId == 0x109AB2; }, "the copy starts on the host's package");
    party.As(party.Guest(), [&] {
        rider.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        rider.Actors.at(cHorse).Ai.PackageId = 0;
        // The native mount changes the package before the host's ownership answer arrives.
        party.Guest().GetDispatcher().trigger(AiStateChangeEvent{cHorse});
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return Owns(party.Guest(), cHorse); }, "the rider now owns the horse");
    party.Until([&] { return host.Actors.at(horse).Ai.PackageId == 0; }, "the host's copy takes the new owner's empty package");
    party.As(party.Guest(), [&] { party.Guest().GetRunner().Trigger(DismountEvent{FakeEngine::kPlayer}); });
    party.Until([&] { return Owns(party.Host(), horse); }, "the dismounted horse returns to the host");
    party.As(party.Host(), [&] {
        host.Actors.at(horse).Ai.PackageId = 0x109AB2;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{horse});
    });
    party.Until([&] { return rider.Actors.at(cHorse).Ai.PackageId == 0x109AB2; }, "the horse's own package returns");
    party.As(party.Guest(), [&] {
        rider.Actors.at(cHorse).Ai.PackageId = 0;
        rider.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return Owns(party.Guest(), cHorse); }, "the same rider owns the horse again");
    party.Until([&] { return host.Actors.at(horse).Ai.PackageId == 0; }, "the empty package is sent again after ownership changes");
}

SCENARIO("The host rides its own horse and both guests put their copies on it", "[mounts]")
{
    Party party(2, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("all three players and their copies are present");
    auto& host = Party::Engine(party.Host());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until(
        [&] {
            return Party::Engine(party.Guest()).CopiesOf(kHorse).size() == 1 &&
                   Party::Engine(party.Guest(1)).CopiesOf(kHorse).size() == 1;
        },
        "both guests have the horse");
    party.As(party.Host(), [&] {
        host.ActorPutOnMount(FakeEngine::kPlayer, horse);
        party.Host().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, horse});
    });
    party.Until(
        [&] { return !Party::Engine(party.Guest()).Mounts.empty() && !Party::Engine(party.Guest(1)).Mounts.empty(); },
        "both guests replay the host's mount interaction");
    CHECK(Owns(party.Host(), horse));
    CHECK_FALSE(host.IsActorCopy(horse));
    for (size_t i = 0; i < 2; ++i)
    {
        const auto& cEngine = Party::Engine(party.Guest(i));
        CHECK(cEngine.IsActorCopy(cEngine.CopiesOf(kHorse)[0]));
    }
}

TEST_CASE("The host dismounts without giving up its own horse", "[mounts]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(kHorse).empty(); }, "the guest has the horse");
    party.As(party.Host(), [&] { party.Host().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, horse}); });
    party.Until([&] { return !Party::Engine(party.Guest()).Mounts.empty(); }, "the guest sees the host riding");
    party.As(party.Host(), [&] { party.Host().GetRunner().Trigger(DismountEvent{FakeEngine::kPlayer}); });
    party.Until([&] { return !host.Actors.at(horse).Persistent; },
                "the host's dismount restores temporary persistence");
    CHECK(Owns(party.Host(), horse));
    CHECK_FALSE(host.IsActorCopy(horse));
    CHECK_FALSE(host.Actors.at(horse).Persistent);
}

SCENARIO("A guest's horse stays its game's until the ride itself ends, by any path", "[mounts][ride-end]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& rider = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return rider.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = rider.CopiesOf(kHorse)[0];
    party.As(party.Guest(), [&] {
        rider.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return Owns(party.Guest(), cHorse) && host.IsActorCopy(horse); }, "the rider's game owns the horse");

    WHEN("its player gets off: the horse is its game's until the exit clip ends")
    {
        party.As(party.Guest(), [&] { rider.StartDismount(FakeEngine::kPlayer, 20); });
        for (int i = 0; i < 10; ++i)
            party.Frame();
        CHECK(rider.IsPlayerMounted());
        CHECK(Owns(party.Guest(), cHorse));
        CHECK(host.IsActorCopy(horse));
        party.Until([&] { return Owns(party.Host(), horse) && !Owns(party.Guest(), cHorse); },
                    "the host owns the horse once the ride ended");
        CHECK_FALSE(rider.IsPlayerMounted());
    }
    WHEN("a fine gets its player off with no Dismount (StopRidingQuick 38705)")
    {
        party.As(party.Guest(), [&] { rider.StopRiding(FakeEngine::kPlayer); });
        CHECK_FALSE(rider.IsPlayerMounted()); // mode 2 forces the finish (40352): off at once
        party.Until([&] { return Owns(party.Host(), horse) && !Owns(party.Guest(), cHorse); },
                    "the host owns the horse again");
    }
    WHEN("a load detaches the ride at once (EndMountInteraction 41535)")
    {
        party.As(party.Guest(), [&] { rider.EndRide(FakeEngine::kPlayer); });
        party.Until([&] { return Owns(party.Host(), horse) && !Owns(party.Guest(), cHorse); },
                    "the host owns the horse again");
    }
}

TEST_CASE("Two guests get on the host's horse at once: the first keeps it, the other's player gets off", "[mounts][one-rider]")
{
    Party party(2, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("all three players and their copies are present");
    auto& host = Party::Engine(party.Host());
    auto& first = Party::Engine(party.Guest());
    auto& second = Party::Engine(party.Guest(1));
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return first.CopiesOf(kHorse).size() == 1 && second.CopiesOf(kHorse).size() == 1; },
                "both guests have the horse");
    const uint32_t cFirstHorse = first.CopiesOf(kHorse)[0];
    const uint32_t cSecondHorse = second.CopiesOf(kHorse)[0];
    // Within one round trip: neither game has heard of the other's ride.
    party.As(party.Guest(), [&] {
        first.ActorPutOnMount(FakeEngine::kPlayer, cFirstHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cFirstHorse});
    });
    party.As(party.Guest(1), [&] {
        second.ActorPutOnMount(FakeEngine::kPlayer, cSecondHorse);
        party.Guest(1).GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cSecondHorse});
    });
    party.Until([&] { return !second.IsPlayerMounted(); }, "the second rider's player gets off");
    party.Until([&] { return Owns(party.Guest(), cFirstHorse) && host.IsActorCopy(horse); }, "the first rider's game owns the horse");
    CHECK(first.IsPlayerMounted());
    CHECK_FALSE(Owns(party.Guest(1), cSecondHorse));
    CHECK(second.IsActorCopy(cSecondHorse));
    // Its getting off is no ride's end the host hears: the first still rides.
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(Owns(party.Guest(), cFirstHorse));
}

TEST_CASE("A guest joining while another rides sees its copy on the horse", "[mounts][late-ride]")
{
    Party party(2, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    }, false);
    party.Join(party.Guest());
    auto& host = Party::Engine(party.Host());
    auto& rider = Party::Engine(party.Guest());
    auto& late = Party::Engine(party.Guest(1));
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return rider.CopiesOf(kHorse).size() == 1; }, "the rider has the horse");
    const uint32_t cHorse = rider.CopiesOf(kHorse)[0];
    party.As(party.Guest(), [&] {
        rider.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return Owns(party.Guest(), cHorse) && !host.Mounts.empty(); }, "the host sees the guest riding");

    party.Join(party.Guest(1));
    party.Until([&] { return late.CopiesOf(kHorse).size() == 1 && !late.Mounts.empty(); },
                "the late guest's copy of the rider is on its horse", [&] {
                    std::string players;
                    for (const auto& [cId, cActor] : late.Actors)
                        if (cActor.Copy)
                            players += std::format("{:X} ", cId);
                    return std::format("horse copies {}, copies {}, mounts {}", late.CopiesOf(kHorse).size(), players, late.Mounts.size());
                });
    const auto cRide = late.Mounts.begin();
    CHECK(cRide->second == late.CopiesOf(kHorse)[0]);
    CHECK(late.IsActorCopy(cRide->first));
    CHECK(late.IsActorCopy(cRide->second));
}

TEST_CASE("A horse passes from rider to rider: the host, a guest, the host again", "[mounts][riders]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    const auto cRide = [&](World& aGame, FakeEngine& aEngine, uint32_t aHorse) {
        party.As(aGame, [&] { aEngine.Activates(FakeEngine::kPlayer, aHorse); });
    };

    cRide(party.Host(), host, horse);
    party.Until([&] { return guest.Mounts.size() == 1; }, "the guest's copy of the host's player rides");
    party.As(party.Host(), [&] { host.StartDismount(FakeEngine::kPlayer); });
    party.Until([&] { return !host.IsPlayerMounted() && guest.Mounts.empty(); }, "the host's player and its copy are off");
    CHECK(Owns(party.Host(), horse));

    cRide(party.Guest(), guest, cHorse);
    party.Until([&] { return Owns(party.Guest(), cHorse) && host.IsActorCopy(horse); }, "the guest's game owns the horse");
    party.Until([&] { return party.Host().GetGraphStreams().OnStream(horse); }, "the host's copy is on the guest's stream");
    party.As(party.Guest(), [&] { guest.StartDismount(FakeEngine::kPlayer); });
    party.Until([&] {
        return std::ranges::any_of(host.Actors.at(horse).Graph.History, [](const auto& acEntry) { return acEntry.State == FakeEngine::kDismountSynced; });
    }, "the host's copy plays the horse's half of the guest's exit");
    party.Until([&] { return Owns(party.Host(), horse) && guest.IsActorCopy(cHorse); }, "the host owns it again");
    // Skyrim (duo, 2026-11-18/19): the guest's last graph ops, its horse's PairEnd among them, went unsent when it gave
    // the horse up; the host's horse stayed in the exit pair, refused the host's get-on and retried it (mounts.md).
    CHECK_FALSE(host.Synced(horse));

    cRide(party.Host(), host, horse);
    party.Until([&] { return guest.Mounts.size() == 1 && guest.Mounts.begin()->second == cHorse; },
                "the host rides it again, the guest's copy on it");
    CHECK(Owns(party.Host(), horse));
    CHECK(host.RefusedGetOns == 0);
    CHECK(host.GettingOnOff.empty());
    CHECK(guest.GettingOnOff.empty());
    party.Agree("the horse and its copies agree");
}

TEST_CASE("A horse taken back over a slow line plays its rider's exit to its end first", "[mounts][riders][hand-back]")
{
    // Skyrim (duo, 2026-11-19): the exit pair's PairEnd (32797) reaches the rider's horse in its Detach's frame, due on
    // its copies with the update after it; a copy plays ~100 ms behind. Taken at the hand-back's arrival, the host's
    // horse missed it, stayed in DismountSynced and refused the host's next get-on five times (mounts.md).
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    }, true, NetConditions{.DelayMs = 80});
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return Owns(party.Guest(), cHorse) && host.IsActorCopy(horse); }, "the guest's game owns the horse");
    party.Until([&] { return party.Host().GetGraphStreams().OnStream(horse); }, "the host's copy is on the guest's stream");
    // Its exit clip runs ~2.4 s (duo): the host's copy is in the horse's half before the ride ends.
    party.As(party.Guest(), [&] { guest.StartDismount(FakeEngine::kPlayer, UINT32_MAX); });
    party.Until([&] { return host.Synced(horse); }, "the host's copy plays the horse's half of the exit");
    party.As(party.Guest(), [&] { guest.EndRide(FakeEngine::kPlayer); });
    party.Until([&] { return Owns(party.Host(), horse); }, "the host owns it again");
    CHECK_FALSE(host.Synced(horse));
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return host.IsPlayerMounted(); }, "the host rides it");
    CHECK(host.RefusedGetOns == 0);
}

TEST_CASE("A guest gets on the horse the host just got off", "[mounts][ride-end][riders]")
{
    // Skyrim (duo, horse-passes-between-riders, 2026-11-17): the guest's Activate did nothing, and the host's copy of
    // the guest kept kGettingOnOffMount (docs/re/systems/mounts.md).
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];

    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return guest.Mounts.size() == 1; }, "the guest's copy of the host's player rides");
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return !host.IsPlayerMounted(); }, "the host's player is off");
    party.Until([&] { return guest.Mounts.empty(); }, "the guest's copy of the host's player is off at its ride's end",
                [&] { return std::format("guest rides {}", guest.Mounts.size()); });

    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return Owns(party.Guest(), cHorse) && host.Mounts.size() == 1; }, "the guest rides, the host sees it");
    CHECK(guest.Mounts.at(FakeEngine::kPlayer) == cHorse);
    CHECK(host.GettingOnOff.empty());
}

TEST_CASE("A ride the engine refuses reserves no horse", "[mounts][one-rider]")
{
    // InitiateMountPackage (37905) returns early when CanRide (38706) refuses (in midair): no ride, no MountEvent.
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];

    party.As(party.Host(), [&] {
        host.Actors.at(FakeEngine::kPlayer).InAir = true;
        host.Activates(FakeEngine::kPlayer, horse);
        host.Actors.at(FakeEngine::kPlayer).InAir = false;
    });
    CHECK_FALSE(host.IsPlayerMounted());
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return Owns(party.Guest(), cHorse) && host.Mounts.size() == 1; }, "the guest rides it");
}

namespace
{
// The host drives its horse; the guest's player boards it with real Activate (docs/re/systems/player-mount.md).
struct Passenger
{
    Party party{1, [](FakeEngine& aEngine) {
                    FakeEngine::MountGraph(aEngine);
                    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
                    aEngine.LoadCell(kCell);
                }};
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    uint32_t guestHorse = 0;
    uint32_t guestOnHost = 0; // the host's copy of the guest's player
    uint32_t hostOnGuest = 0;

    Passenger()
    {
        party.Agree("the players are present");
        party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
        party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
        guestHorse = guest.CopiesOf(kHorse)[0];
        for (const auto& [cId, cActor] : host.Actors)
            if (cActor.Copy && cActor.Player)
                guestOnHost = cId;
        for (const auto& [cId, cActor] : guest.Actors)
            if (cActor.Copy && cActor.Player)
                hostOnGuest = cId;
        party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
        party.Until([&] { return guest.GetActorMount(hostOnGuest) == guestHorse; }, "the guest sees the host riding");
        party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, guestHorse); });
        // The host's yes seats it at once (PutActorOnMountQuick); its Ride part seats its copy (player-mount.md).
        party.Until([&] { return guest.SitState(FakeEngine::kPlayer) == ::ReplayState::kSitSeated &&
                                 host.GetActorMount(guestOnHost) == horse && host.SitState(guestOnHost) == ::ReplayState::kSitSeated; },
                    "the guest is seated on both games");
    }
};
} // namespace

TEST_CASE("A guest boards the horse the host drives: seated on both games, the host still driving", "[mounts][passengers]")
{
    Passenger ride;
    CHECK(ride.guest.GetActorMount(FakeEngine::kPlayer) == ride.guestHorse);
    CHECK(ride.host.PlayerSteers(ride.horse));
    CHECK(Owns(ride.party.Host(), ride.horse));
    CHECK(ride.guest.IsActorCopy(ride.guestHorse));
    // Its seat places it, not its owner's snapshots: its owner rides the guest's lagging horse copy, and steering it
    // there dragged the host's horse off across Skyrim (the duo, 2026-11-17).
    const uint32_t cMovements = ride.host.Actors.at(ride.guestOnHost).Movements;
    const float cX = ride.host.Actors.at(ride.guestOnHost).Position[0];
    const auto cStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - cStart < std::chrono::milliseconds(600)) // the session's clock
    {
        ride.party.As(ride.party.Guest(), [&] { ride.guest.Actors.at(FakeEngine::kPlayer).Position[0] += 5.f; });
        ride.party.Frame();
    }
    CHECK(ride.host.Actors.at(ride.guestOnHost).Movements == cMovements);
    CHECK(ride.host.Actors.at(ride.guestOnHost).Position[0] == cX);
    // Its owner's HorseExit (sit state 4): off the horse here at once, its clip left to end, then led down by its owner's
    // place (attached, a step threw it off the horse; left, it stood on the horse until its clip's end: the duo).
    {
        auto& copy = ride.host.Actors.at(ride.guestOnHost);
        const uint32_t cBefore = copy.Movements;
        copy.State1 = (copy.State1 & ~(0xFu << FakeEngine::kSitShift)) | 4u << FakeEngine::kSitShift;
        ride.party.Until([&] { return ride.host.GetActorMount(ride.guestOnHost) == 0; }, "the copy is off its mount");
        const auto cOff = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - cOff < std::chrono::milliseconds(600))
        {
            ride.party.As(ride.party.Guest(), [&] { ride.guest.Actors.at(FakeEngine::kPlayer).Position[0] += 5.f; });
            ride.party.Frame();
        }
        CHECK(copy.Movements > cBefore);
        CHECK(ride.host.SitState(ride.guestOnHost) == 4); // its clip's to end (ChairFurnitureExitHandler 42835)
    }

}

TEST_CASE("Players on one horse pass through each other's blows; off it, a blow lands", "[mounts][passengers][friendly-fire]")
{
    Passenger ride;
    const auto cHealth = [](FakeEngine& aEngine) { return aEngine.GetActorValue(FakeEngine::kPlayer, FakeEngine::kHealth); };
    const float cGuestFull = cHealth(ride.guest);
    const float cHostFull = cHealth(ride.host);
    // The host's sword on the guest behind it, the guest's back: neither game's player takes anything.
    ride.party.As(ride.party.Host(), [&] { ride.host.Strikes(FakeEngine::kPlayer, ride.guestOnHost, 30.f); });
    ride.party.As(ride.party.Guest(), [&] { ride.guest.Strikes(FakeEngine::kPlayer, ride.hostOnGuest, 30.f); });
    for (int i = 0; i < 20; ++i)
        ride.party.Frame();
    CHECK(cHealth(ride.guest) == cGuestFull);
    CHECK(cHealth(ride.host) == cHostFull);

    // The guest gets off: the host's blow lands again (friendly fire, docs/progress.md).
    ride.party.As(ride.party.Guest(), [&] { ride.guest.Activates(FakeEngine::kPlayer, ride.guestHorse); });
    ride.party.Until([&] { return !ride.guest.IsPlayerMounted(); }, "the passenger is off");
    ride.party.As(ride.party.Host(), [&] { ride.host.EndRide(ride.guestOnHost); });
    ride.party.Until([&] { return ride.host.GetActorMount(ride.guestOnHost) == 0; }, "the guest's copy is off on the host");
    ride.party.As(ride.party.Host(), [&] { ride.host.Strikes(FakeEngine::kPlayer, ride.guestOnHost, 30.f); });
    ride.party.Until([&] { return cHealth(ride.guest) == cGuestFull - 30.f; }, "the guest's own player takes the host's blow");
}

TEST_CASE("A passenger sits at once: no get-on clip on any game, the driver keeps its horse", "[mounts][passengers]")
{
    Passenger ride;
    // The engine's quick get-on (IDLE EnterHorseInstant): no HorseEnter, no pair with the horse (player-mount.md).
    CHECK(ride.guest.GetActorLatestAnimation(FakeEngine::kPlayer).EventName == "HorseEnterInstant");
    ride.party.Until([&] { return ride.host.GetActorLatestAnimation(ride.guestOnHost).EventName == "HorseEnterInstant"; },
                     "the host's copy has its owner's quick get-on");
    for (int i = 0; i < 10; ++i)
        ride.party.Frame();
    CHECK(ride.host.SitState(ride.guestOnHost) == ::ReplayState::kSitSeated);
    CHECK(ride.host.SeatedRiders.contains(ride.guestOnHost)); // its reference rides along (20318)
    CHECK(ride.guest.SeatedRiders.contains(FakeEngine::kPlayer));
    CHECK(ride.guest.MountSlots.at(ride.guestHorse) == ride.hostOnGuest);
    CHECK(ride.host.MountSlots.at(ride.horse) == FakeEngine::kPlayer);
    CHECK_FALSE(ride.guest.Synced(ride.guestHorse));
    CHECK(ride.host.PlayerSteers(ride.horse));
}

TEST_CASE("A passenger's copy its stream sat first is still seated by its Ride part", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    uint32_t guestOnHost = 0;
    for (const auto& [cId, cActor] : host.Actors)
        if (cActor.Copy && cActor.Player)
            guestOnHost = cId;
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return !guest.Mounts.empty(); }, "the guest sees the host riding");
    // On the duo its stream's HorseEnterInstant (39004: sit state 3) came before its Ride part was due.
    party.As(party.Host(), [&] {
        host.ActorPutOnMount(guestOnHost, horse);
        auto& copy = host.Actors.at(guestOnHost);
        copy.State1 = (copy.State1 & ~(0xFu << FakeEngine::kSitShift)) | uint32_t(::ReplayState::kSitSeated) << FakeEngine::kSitShift;
    });
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return host.SeatedRiders.contains(guestOnHost); }, "SeatRider ran for the host's copy (its reference rides along, 20318)");
}

TEST_CASE("A board the host refuses leaves the player standing", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    }, true, NetConditions{.DelayMs = 300}); // the guest hears the ride's end long after the host's game ends it
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return !guest.Mounts.empty(); }, "the guest sees the host riding");
    // The host's ride ends (no exit clip, 38705) and its game gives the horse back; the guest asks before it hears:
    // nobody drives it when the host hears the guest.
    party.As(party.Host(), [&] { host.EndRide(FakeEngine::kPlayer); });
    const auto cRun = [&](int aMs) {
        const auto cStart = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - cStart < std::chrono::milliseconds(aMs)) // the session's clock
            party.Frame();
    };
    cRun(100);
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    cRun(900); // there and back over the slow line
    CHECK_FALSE(guest.IsPlayerMounted());
    CHECK(guest.SitState(FakeEngine::kPlayer) == 0);
    // Asked again, now with the horse its own: a ride of its own.
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return Owns(party.Guest(), cHorse); }, "the guest rides the horse itself");
}

TEST_CASE("A seat granted after the player took a horse of its own is given back", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t driven = 0, spare = 0;
    party.As(party.Host(), [&] {
        driven = host.LoadNpc(kHorse, kCell);
        spare = host.LoadNpc(kHorse, kCell);
    });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 2; }, "the guest has both horses");
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, driven); });
    party.Until([&] { return !guest.Mounts.empty(); }, "the guest sees the host riding");
    const uint32_t cDriven = guest.Mounts.begin()->second;
    const uint32_t cSpare = guest.CopiesOf(kHorse)[0] == cDriven ? guest.CopiesOf(kHorse)[1] : guest.CopiesOf(kHorse)[0];
    // Asks for a seat, then gets on the free horse before the host's answer comes back.
    party.As(party.Guest(), [&] {
        guest.Activates(FakeEngine::kPlayer, cDriven);
        guest.Activates(FakeEngine::kPlayer, cSpare);
    });
    party.Until([&] { return Owns(party.Guest(), cSpare); }, "the guest rides the free horse");
    for (int i = 0; i < 20; ++i)
        party.Frame();
    CHECK(guest.GetActorMount(FakeEngine::kPlayer) == cSpare);
    CHECK(guest.MountSlots.at(cDriven) != FakeEngine::kPlayer);
    CHECK(guest.GetActorLatestAnimation(FakeEngine::kPlayer).EventName != "HorseEnterInstant");
    (void)spare;
}

TEST_CASE("A guest joining while a passenger rides sees it seated on the horse", "[mounts][passengers][late-ride]")
{
    Party party(2, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    }, false);
    party.Join(party.Guest());
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& late = Party::Engine(party.Guest(1));
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return !guest.Mounts.empty(); }, "the guest sees the host riding");
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return guest.SitState(FakeEngine::kPlayer) == ::ReplayState::kSitSeated; }, "the guest is a passenger");

    // Its Ride part rides on its spawn; seated once both it and the horse are here (R5).
    party.Join(party.Guest(1));
    party.Until([&] { return late.CopiesOf(kHorse).size() == 1 && late.Mounts.size() == 2; }, "both riders are on the late guest's horse",
                [&] { return std::format("horse copies {}, mounts {}", late.CopiesOf(kHorse).size(), late.Mounts.size()); });
    const uint32_t cLateHorse = late.CopiesOf(kHorse)[0];
    std::optional<uint32_t> guestSession;
    party.As(party.Guest(), [&] { guestSession = Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    uint32_t passenger = 0, driver = 0;
    for (const auto& [cRider, cMount] : late.Mounts)
    {
        CHECK(cMount == cLateHorse);
        std::optional<uint32_t> session;
        party.As(party.Guest(1), [&] { session = Utils::GetSessionIdByFormId(cRider); });
        (session == guestSession ? passenger : driver) = cRider;
    }
    REQUIRE(passenger);
    REQUIRE(driver);
    party.Until([&] { return late.SeatedRiders.contains(passenger); }, "the passenger's copy is seated");
    // The spawns come before the ride (SendRides): the horse still names its driver (12074, its one slot).
    CHECK(late.MountSlots.at(cLateHorse) == driver);
}

TEST_CASE("The driver gets off: the passenger drives, the horse its game's", "[mounts][passengers]")
{
    Passenger ride;
    CHECK_FALSE(ride.guest.PlayerSteers(ride.guestHorse));
    ride.party.As(ride.party.Host(), [&] { ride.host.StartDismount(FakeEngine::kPlayer); });
    ride.party.Until([&] { return Owns(ride.party.Guest(), ride.guestHorse) && ride.host.IsActorCopy(ride.horse); },
                     "the passenger's game owns the horse");
    // The host's copy finishes its exit here after the horse changed hands: the horse is left in that pair (duo).
    ride.party.As(ride.party.Guest(), [&] { ride.guest.EndRide(ride.hostOnGuest); });
    ride.party.Until([&] { return ride.guest.PlayerSteers(ride.guestHorse); }, "the passenger steers it",
                     [&] { return std::format("slot {} reins {} synced {}", ride.guest.MountSlots.size(), ride.guest.Reins.size(),
                                              ride.guest.Synced(ride.guestHorse)); });
    // The new driver's copy rides its seat too: placed by its snapshots it shook on the saddle (the duo, 2026-11-17).
    const uint32_t cMovements = ride.host.Actors.at(ride.guestOnHost).Movements;
    const auto cStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - cStart < std::chrono::milliseconds(600))
    {
        ride.party.As(ride.party.Guest(), [&] { ride.guest.Actors.at(FakeEngine::kPlayer).Position[0] += 5.f; });
        ride.party.Frame();
    }
    CHECK(ride.host.Actors.at(ride.guestOnHost).Movements == cMovements);
    // The host's copy of it stays on (seated already) and the horse names it there.
    CHECK(ride.host.GetActorMount(ride.guestOnHost) == ride.horse);
    CHECK(ride.host.MountSlots.at(ride.horse) == ride.guestOnHost);

    ride.party.As(ride.party.Guest(), [&] { ride.guest.StartDismount(FakeEngine::kPlayer); });
    ride.party.Until([&] { return Owns(ride.party.Host(), ride.horse); }, "no passenger left: the host's again");
}

TEST_CASE("The driver's copy is off before the passenger takes the reins: the passenger keeps steering", "[mounts][passengers]")
{
    Passenger ride;
    // The host's copy finishes its exit before the horse changes hands here.
    ride.party.As(ride.party.Guest(), [&] { ride.guest.EndRide(ride.hostOnGuest); });
    ride.party.As(ride.party.Host(), [&] { ride.host.StartDismount(FakeEngine::kPlayer); });
    ride.party.Until([&] { return ride.guest.PlayerSteers(ride.guestHorse); }, "the passenger steers it");
    CHECK_FALSE(ride.guest.Synced(ride.guestHorse));
}

TEST_CASE("A passenger gets off: the driver keeps its horse on both games", "[mounts][passengers]")
{
    Passenger ride;
    ride.party.As(ride.party.Guest(), [&] { ride.guest.Activates(FakeEngine::kPlayer, ride.guestHorse); });
    ride.party.Until([&] { return !ride.guest.IsPlayerMounted(); }, "the passenger is off");
    // Its own Detach emptied its horse's slot, the host's copy still on: held again.
    ride.party.Until([&] { return ride.guest.MountSlots.contains(ride.guestHorse) && ride.guest.MountSlots.at(ride.guestHorse) == ride.hostOnGuest; },
                     "the guest's horse names the driver's copy again");
    // Its copy's exit clip ends on the host: the same there.
    ride.party.As(ride.party.Host(), [&] { ride.host.EndRide(ride.guestOnHost); });
    ride.party.Until([&] { return ride.host.MountSlots.contains(ride.horse) && ride.host.MountSlots.at(ride.horse) == FakeEngine::kPlayer; },
                     "the host's horse names the host again");
    // Off its mount, its owner's movement places it again (on it, its engine moved it: the seat, its clips).
    const uint32_t cMovements = ride.host.Actors.at(ride.guestOnHost).Movements;
    const auto cStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - cStart < std::chrono::milliseconds(600))
    {
        ride.party.As(ride.party.Guest(), [&] { ride.guest.Actors.at(FakeEngine::kPlayer).Position[0] += 5.f; });
        ride.party.Frame();
    }
    CHECK(ride.host.Actors.at(ride.guestOnHost).Movements > cMovements);
    CHECK(ride.host.PlayerSteers(ride.horse));

    // No passenger now: the driver's getting off gives the horse back as before.
    ride.party.As(ride.party.Host(), [&] { ride.host.StartDismount(FakeEngine::kPlayer); });
    ride.party.Until([&] { return !ride.host.IsPlayerMounted(); }, "the host is off");
    for (int i = 0; i < 10; ++i)
        ride.party.Frame();
    CHECK(Owns(ride.party.Host(), ride.horse));
    CHECK_FALSE(ride.guest.IsPlayerMounted());
}

TEST_CASE("A passenger's ride ends with no exit clip: its copy gets off on the driver's game", "[mounts][passengers]")
{
    Passenger ride;
    // A forced end (38705/40352, mounts.md): no HorseExit replays for its copy.
    ride.party.As(ride.party.Guest(), [&] { ride.guest.EndRide(FakeEngine::kPlayer); });
    ride.party.Until([&] { return ride.host.GetActorMount(ride.guestOnHost) == 0; }, "the host's copy of the guest is off");
    ride.party.Until([&] { return ride.host.MountSlots.contains(ride.horse) && ride.host.MountSlots.at(ride.horse) == FakeEngine::kPlayer; },
                     "the host's horse names the host again");
    CHECK(ride.host.PlayerSteers(ride.horse));
}

TEST_CASE("The driver's ride ends with no exit clip: the passenger drives, the driver's copy off", "[mounts][passengers]")
{
    Passenger ride;
    ride.party.As(ride.party.Host(), [&] { ride.host.EndRide(FakeEngine::kPlayer); });
    ride.party.Until([&] { return ride.guest.GetActorMount(ride.hostOnGuest) == 0; }, "the guest's copy of the host is off");
    ride.party.Until([&] { return ride.guest.PlayerSteers(ride.guestHorse); }, "the passenger steers it");
}

TEST_CASE("A get-on the engine refuses leaves the horse its driver's", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cGuestHorse = guest.CopiesOf(kHorse)[0];
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return guest.MountSlots.contains(cGuestHorse); }, "the guest sees the host riding");
    const uint32_t cDriverCopy = guest.MountSlots.at(cGuestHorse);
    // The host's yes comes, then the engine's quick get-on refuses: the seat goes back, the horse names its driver.
    guest.RefuseQuickSeat = true;
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cGuestHorse); });
    party.Until([&] { return !guest.IsPlayerMounted() && guest.MountSlots.contains(cGuestHorse) && guest.MountSlots.at(cGuestHorse) == cDriverCopy; },
                "the guest stands, the horse names its driver again");
    // Its seat given back (UnboardRequest): the host's ride ending hands the horse to nobody.
    party.As(party.Host(), [&] { host.EndRide(FakeEngine::kPlayer); });
    party.Until([&] { return !guest.MountSlots.contains(cGuestHorse); }, "nobody rides the horse");
    CHECK_FALSE(Owns(party.Guest(), cGuestHorse));
}

TEST_CASE("No boarding a horse an NPC rides", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0, rider = 0;
    party.As(party.Host(), [&] {
        horse = host.LoadNpc(kHorse, kCell);
        rider = host.LoadNpc(0x1A699, kCell);
    });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1 && guest.CopiesOf(0x1A699).size() == 1; }, "the guest has both");
    party.As(party.Host(), [&] {
        host.ActorPutOnMount(rider, horse);
        party.Host().GetRunner().Trigger(MountEvent{rider, horse});
    });
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    party.Until([&] { return guest.MountSlots.contains(cHorse); }, "the guest sees the NPC riding");
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK_FALSE(guest.IsPlayerMounted());
}

TEST_CASE("Driver and passenger get off at once: the horse handed to the one already off goes back", "[mounts][passengers]")
{
    // The host can hand the horse on before the passenger's UnboardRequest arrives (both off within a round trip, or
    // a horse's death dropping every rider): that game would own a horse nobody rides, for good.
    Passenger ride;
    // The host's ride ends and its rule hands the horse on; the guest's ride ends and is handled before that arrives.
    ride.party.As(ride.party.Host(), [&] { ride.host.EndRide(FakeEngine::kPlayer); });
    for (int i = 0; i < 3; ++i)
        ride.party.Step(ride.party.Host());
    ride.party.As(ride.party.Guest(), [&] { ride.guest.EndRide(FakeEngine::kPlayer); });
    ride.party.Until([&] { return Owns(ride.party.Host(), ride.horse) && ride.guest.IsActorCopy(ride.guestHorse); },
                     "the host owns the horse again, nobody on it", [&] {
                         return std::format("host owns {} guest owns {}", Owns(ride.party.Host(), ride.horse),
                                            Owns(ride.party.Guest(), ride.guestHorse));
                     });
    CHECK_FALSE(ride.guest.IsPlayerMounted());
    for (int i = 0; i < 20; ++i)
        ride.party.Frame();
    CHECK(Owns(ride.party.Host(), ride.horse));
}

TEST_CASE("The horse handed to a passenger no longer on it goes back to the host", "[mounts][passengers]")
{
    // Its ride ended unheard (a native MoveTo unlinks with no Detach, party-travel.md): no UnboardRequest went out.
    Passenger ride;
    ride.party.As(ride.party.Guest(), [&] { ride.guest.Mounts.erase(FakeEngine::kPlayer); });
    ride.party.As(ride.party.Host(), [&] { ride.host.StartDismount(FakeEngine::kPlayer); });
    ride.party.Until([&] { return !ride.host.IsPlayerMounted(); }, "the host is off");
    for (int i = 0; i < 30; ++i)
        ride.party.Frame();
    ride.party.Until([&] { return Owns(ride.party.Host(), ride.horse) && ride.guest.IsActorCopy(ride.guestHorse); },
                     "the horse is the host's again, nobody riding it",
                     [&] { return std::format("guest owns {}", Owns(ride.party.Guest(), ride.guestHorse)); });
}

TEST_CASE("A guest joining while a passenger rides along sees both on the horse", "[mounts][passengers][late-ride]")
{
    Party party(2, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    }, false);
    party.Join(party.Guest());
    auto& host = Party::Engine(party.Host());
    auto& rider = Party::Engine(party.Guest());
    auto& late = Party::Engine(party.Guest(1));
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return rider.CopiesOf(kHorse).size() == 1; }, "the rider has the horse");
    const uint32_t cHorse = rider.CopiesOf(kHorse)[0];
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return rider.MountSlots.contains(cHorse); }, "the guest sees the host riding");
    party.As(party.Guest(), [&] { rider.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return host.Mounts.size() == 2; }, "the host sees its passenger");

    party.Join(party.Guest(1));
    party.Until([&] { return late.CopiesOf(kHorse).size() == 1 && late.Mounts.size() == 2; },
                "the late guest's copies of both players are on its horse",
                [&] { return std::format("horse copies {}, riders {}", late.CopiesOf(kHorse).size(), late.Mounts.size()); });
    const uint32_t cLateHorse = late.CopiesOf(kHorse)[0];
    // Linked: each copy's seat is its sit state's, from its owner's spawn state and actions (MountInteraction::Update
    // 40356 seats a linked rider at sit state 3), which the fake's spawn does not carry.
    for (const auto& [cRider, cMount] : late.Mounts)
        CHECK(cMount == cLateHorse);
}

TEST_CASE("The host rides along on the horse a guest drives, then drives it when the guest gets off", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
        FakeEngine::MountGraph(aEngine);
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
    });
    party.Agree("the players are present");
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cHorse); });
    party.Until([&] { return Owns(party.Guest(), cHorse) && host.MountSlots.contains(horse); }, "the guest drives, the host sees it");
    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, horse); });
    party.Until([&] { return host.GetActorMount(FakeEngine::kPlayer) == horse && guest.Mounts.size() == 2; },
                "the host rides along, seated on the guest's game");
    CHECK(Owns(party.Guest(), cHorse));

    party.As(party.Guest(), [&] { guest.StartDismount(FakeEngine::kPlayer); });
    party.Until([&] { return Owns(party.Host(), horse) && host.PlayerSteers(horse); }, "the host drives its horse again",
                [&] { return std::format("host owns {} slot {} reins {}", Owns(party.Host(), horse), host.MountSlots.contains(horse),
                                         host.Reins.contains(horse)); });
}

namespace
{
// A host horse and the guest's copy of it, both games on the mount graph.
struct HorseParty
{
    Party party{1, [](FakeEngine& aEngine) {
                    FakeEngine::MountGraph(aEngine);
                    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
                    aEngine.LoadCell(kCell);
                }};
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    uint32_t copy = 0;

    HorseParty()
    {
        party.Agree("the players are present");
        party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kCell); });
        party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the horse");
        copy = guest.CopiesOf(kHorse)[0];
    }
    void GuestRides() { party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, copy); }); }
};
} // namespace

SCENARIO("A horse whose copy's stream stops on the host's game is still taken back, and ridden again", "[mounts][claims]")
{
    // HostTakeBacks waits for the copy to replay its holder's ops; a copy whose graph stops updating (still loaded)
    // never gets there. Before, the horse stayed on its way back for good and every later ride was refused.
    HorseParty ride;
    ride.GuestRides();
    ride.party.Until([&] { return Owns(ride.party.Guest(), ride.copy) && ride.party.Host().GetGraphStreams().OnStream(ride.horse); },
                     "the guest's game holds the horse, the host's copy on its stream");
    ride.host.Actors.at(ride.horse).GraphUpdating = false;
    ride.party.As(ride.party.Guest(), [&] { ride.guest.StartDismount(FakeEngine::kPlayer); });
    ride.party.Until([&] { return Owns(ride.party.Host(), ride.horse); }, "the host's game takes it back anyway", {}, nullptr,
                     std::chrono::seconds(10));
    ride.host.Actors.at(ride.horse).GraphUpdating = true;
    ride.GuestRides();
    ride.party.Until([&] { return Owns(ride.party.Guest(), ride.copy); }, "the guest rides it again", {}, nullptr, std::chrono::seconds(10));
}

SCENARIO("A ride on the host player's summoned horse is refused before the host gives it up", "[mounts][claims]")
{
    // No game takes a player's summon (CharacterService::TakeOwnership): the host's game could not take it back either.
    HorseParty ride;
    ride.party.As(ride.party.Host(), [&] { ride.host.Actors.at(ride.horse).Commander = FakeEngine::kPlayer; });
    ride.guest.Actors.at(ride.copy).Commander = FakeEngine::kPlayer;
    ride.GuestRides();
    ride.party.Until([&] { return !ride.guest.IsPlayerMounted(); }, "the guest's player is off it, its ride refused");
    CHECK(Owns(ride.party.Host(), ride.horse));
    CHECK(ride.guest.IsActorCopy(ride.copy));
}

SCENARIO("A ride its game could not take ends there too: its player gets off", "[mounts][claims]")
{
    // The guest's game alone refuses the horse (a summon to it): the claim comes straight back, the host ends the ride
    // (HostClaims::OnEnded), and the guest's player, seated already, gets off.
    HorseParty ride;
    ride.guest.Actors.at(ride.copy).Commander = FakeEngine::kPlayer;
    ride.GuestRides();
    ride.party.Until([&] { return ride.guest.IsPlayerMounted(); }, "the guest's player gets on");
    ride.party.Until([&] { return !ride.guest.IsPlayerMounted(); }, "the guest's player is off it");
    ride.party.Until([&] { return Owns(ride.party.Host(), ride.horse); }, "the host's game has it");
    CHECK(ride.guest.IsActorCopy(ride.copy));
}

SCENARIO("The host's player rides its own summoned horse", "[mounts][claims]")
{
    // No game takes a player's summon, but this ride moves nothing: the horse is the host's game's already.
    HorseParty ride;
    ride.party.As(ride.party.Host(), [&] {
        ride.host.Actors.at(ride.horse).Commander = FakeEngine::kPlayer;
        ride.host.Activates(FakeEngine::kPlayer, ride.horse);
    });
    ride.party.Until([&] { return ride.guest.Mounts.size() == 1; }, "the guest's copy of the host's player rides");
    int frames = 0;
    ride.party.Until([&] { return ++frames > 30; }, "the claim's round trip");
    CHECK(ride.host.IsPlayerMounted());
    CHECK(Owns(ride.party.Host(), ride.horse));
}

