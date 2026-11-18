#include "Party.h"

#include <Events/DismountEvent.h>
#include <Events/MountEvent.h>
#include <Games/Animation/ReplayState.h>
#include <Events/AiStateChangeEvent.h>

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
    party.As(party.Guest(), [&] { guest.StartDismount(FakeEngine::kPlayer); });
    party.Until([&] { return Owns(party.Host(), horse) && guest.IsActorCopy(cHorse); }, "the host owns it again");

    cRide(party.Host(), host, horse);
    party.Until([&] { return guest.Mounts.size() == 1 && guest.Mounts.begin()->second == cHorse; },
                "the host rides it again, the guest's copy on it");
    CHECK(Owns(party.Host(), horse));
    CHECK(host.GettingOnOff.empty());
    CHECK(guest.GettingOnOff.empty());
    party.Agree("the horse and its copies agree");
}

TEST_CASE("A guest gets on the horse the host just got off", "[mounts][ride-end][riders]")
{
    // Skyrim (duo, horse-passes-between-riders, 2026-11-17): the guest's Activate did nothing, and the host's copy of
    // the guest kept kGettingOnOffMount (docs/re/systems/mounts.md).
    Party party(1, [](FakeEngine& aEngine) {
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
        // Its owner's get-on pair holds it off its stream on the host (PairOnStream) until the pair ends.
        auto& streams = party.Host().GetGraphStreams();
        party.As(party.Host(), [&] { streams.HoldWhile(guestOnHost, true); });
        party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, guestHorse); });
        party.Until([&] { return host.GetActorMount(guestOnHost) == horse; }, "the host's copy of the guest is linked to the horse");
        // Linked, not seated: popped onto the saddle at once it played its whole get-on up there (the duo, 174 units).
        for (int i = 0; i < 5; ++i)
            party.Frame();
        REQUIRE(host.SitState(guestOnHost) == ::ReplayState::kSitGettingOn);
        party.As(party.Host(), [&] { streams.HoldWhile(guestOnHost, false); });
        party.Until([&] { return host.SitState(guestOnHost) == ::ReplayState::kSitSeated; }, "its hold's end seats it");
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
        // Seated again for the steps below (as NotifyBoard does).
        ride.party.As(ride.party.Host(), [&] { ride.host.ActorSeatOnMount(ride.guestOnHost, ride.horse); });
    }

    // Its owner's get-on replayed after: 39004 would set sit state 2, and no horse half would end the clip.
    ActionEvent enter;
    enter.ActionId = 0x13007;
    enter.EventName = "HorseEnter";
    enter.TargetEventName = "HorseEnterPartner";
    enter.TargetId = ride.guestHorse;
    ride.party.As(ride.party.Guest(), [&] {
        auto& player = ride.guest.Actors.at(FakeEngine::kPlayer);
        player.State1 = (player.State1 & ~(0xFu << FakeEngine::kSitShift)) | 2u << FakeEngine::kSitShift;
        ride.guest.PerformAnimation(FakeEngine::kPlayer, enter);
    });
    ride.party.Until([&] { return ride.host.GetActorLatestAnimation(ride.guestOnHost).EventName == "HorseEnter"; },
                     "the host's copy has the get-on");
    CHECK(ride.host.SitState(ride.guestOnHost) == 3);
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
                                              ride.guest.SyncedMounts.size()); });
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
    // Its slot emptied for the get-on, then CanRide (38706) refuses: midair.
    guest.Actors.at(FakeEngine::kPlayer).InAir = true;
    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, cGuestHorse); });
    for (int i = 0; i < 5; ++i)
        party.Frame();
    CHECK_FALSE(guest.IsPlayerMounted());
    REQUIRE(guest.MountSlots.contains(cGuestHorse));
    CHECK(guest.MountSlots.at(cGuestHorse) == cDriverCopy);
}

TEST_CASE("No boarding a horse an NPC rides", "[mounts][passengers]")
{
    Party party(1, [](FakeEngine& aEngine) {
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
