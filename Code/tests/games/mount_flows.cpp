#include "Party.h"

#include <Events/DismountEvent.h>
#include <Events/MountEvent.h>
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
    // Skyrim (duo, horse-passes-between-riders, 2026-11-17): the guest's Activate did nothing. Its copy of the host's
    // player was still on the horse (its replayed exit clip ends after its owner's ride), so the horse read ridden
    // (Character::ActivateMount 37864). And the activation went to the host, whose replay started a mount package on
    // its copy of the guest's player: kGettingOnOffMount for good.
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
