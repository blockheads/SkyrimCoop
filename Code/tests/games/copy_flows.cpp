// Copies across games (docs/design/copy-contract.md): an NPC the host's game loads gets a copy on each guest's game;
// a guest's player gets a copy on the host's; each goes, or follows, when its owner's actor does. Each ends with the
// actor checksum (Party::Agree).
#include "Party.h"

#include <Components.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // the cell everyone stands in
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior
constexpr uint32_t kGuard = 0x0001A67C;    // an NPC's base (a guard)
constexpr uint32_t kVilkas = 0x0001A695;   // an NPC placed in kSmoke (the same reference in every game)
constexpr uint32_t kVilkasBase = 0x0001A694;
constexpr uint32_t kAela = 0x0001A697; // an NPC placed in kSinkhole
constexpr uint32_t kAelaBase = 0x0001A696;

// That game simulates the actor and owns it in the session (CharacterService's LocalComponent).
bool OwnsHere(World& aGame, uint32_t aFormId)
{
    for (const auto cEntity : aGame.view<FormIdComponent, LocalComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aFormId)
            return true;
    return false;
}

// That game holds its reference as a copy of a host NPC that went (CharacterService::FollowOwner).
bool HeldHere(World& aGame, uint32_t aFormId)
{
    for (const auto cEntity : aGame.view<FormIdComponent, HeldCopyComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aFormId)
            return true;
    return false;
}

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

// The plugins' placed NPCs too: Vilkas in QASmoke, Aela in the sinkhole.
void PlacedNpcs(FakeEngine& aEngine)
{
    Plugins(aEngine);
    aEngine.PlaceNpc(kVilkas, kVilkasBase, kSmoke, {100.f, 0.f, 0.f});
    aEngine.PlaceNpc(kAela, kAelaBase, kSinkhole, {-50.f, 20.f, 0.f});
}
} // namespace

SCENARIO("An NPC the host's game loads gets a copy on the guest's game", "[copies]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return !guest.CopiesOf(kGuard).empty(); }, "the guest's game makes a copy of the host's guard");
    THEN("one copy, in the cell, alive, and the host's guard is no copy")
    {
        CHECK(guest.CopiesOf(kGuard).size() == 1);
        CHECK(guest.Actors.at(guest.CopiesOf(kGuard)[0]).Cell == kSmoke);
        CHECK(!host.Actors.at(npc).Copy);
        party.Agree("the games agree on the guard");
    }

    WHEN("the host's guard goes (its cell unloads it) before the host's game has its assignment back")
    {
        // The host's rules sent the spawn with the assignment: the guest has its copy a frame before the host's
        // game knows it owns the guard (its answer is handled on its next update). Loopback's timing only.
        if (party.Conditioned())
        {
            WARN("left out under COOP_NET: the guest's copy comes after the host's answer");
            return;
        }
        REQUIRE(!OwnsHere(party.Host(), npc));
        party.As(party.Host(), [&] { host.Actors.erase(npc); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy goes");
        party.Agree("the games agree on the actors left");
    }

    WHEN("the host's guard goes once the host's game owns it")
    {
        party.Until([&] { return OwnsHere(party.Host(), npc); }, "the host's game owns its guard");
        party.As(party.Host(), [&] { host.Actors.erase(npc); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy goes");
        party.Agree("the games agree on the actors left");
    }
}

SCENARIO("A guest's player gets a copy on the host's game", "[copies]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());

    const auto cPlayerCopies = [&] {
        size_t copies = 0;
        for (const uint32_t cId : host.CopiesOf())
            copies += host.Actors.at(cId).Player;
        return copies;
    };
    party.Until([&] { return cPlayerCopies() == 1; }, "the host's game makes a copy of the guest's player");
    party.Agree("each game has the other's player");

    WHEN("the guest leaves")
    {
        party.Leave(party.Guest());
        party.Until([&] { return cPlayerCopies() == 0; }, "the host's copy of the guest's player goes");
    }
}

SCENARIO("The host's NPC goes away: its copy does what it does", "[copies]")
{
    Party party(1, PlacedNpcs);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {-30.f, 10.f, 0.f}); });
    party.Until([&] { return guest.IsActorCopy(kVilkas) && guest.CopiesOf(kGuard).size() == 1; },
                "the guest's Vilkas is the host's copy, and it has the host's guard");
    party.Agree("the games agree on Vilkas and the guard");

    WHEN("a quest moves the host's Vilkas to the sinkhole, which nobody has loaded")
    {
        party.As(party.Host(), [&] { host.MoveActorTo(kVilkas, kSinkhole, 300.f, 300.f, 0.f); });
        party.Until([&] { return guest.Actors.at(kVilkas).Cell == kSinkhole; },
                    "the guest's Vilkas follows to the sinkhole",
                    [&] { return std::format("the guest's Vilkas in {:X}", guest.Actors.at(kVilkas).Cell); });
        THEN("it is the guest's reference still, held as a copy")
        {
            CHECK(guest.IsActorCopy(kVilkas));
            CHECK(HeldHere(party.Guest(), kVilkas));
            party.Agree("the games agree on the moved Vilkas");
        }

        AND_WHEN("he comes back")
        {
            party.As(party.Host(), [&] { host.MoveActorTo(kVilkas, kSmoke, 120.f, 10.f, 0.f); });
            party.Until([&] { return OwnsHere(party.Host(), kVilkas) && !HeldHere(party.Guest(), kVilkas); },
                        "the host's game owns Vilkas again and the guest's copy is its copy again");
            party.Agree("the games agree on Vilkas back");
        }
    }

    WHEN("the host's Vilkas is disabled")
    {
        party.As(party.Host(), [&] { host.DisableActor(kVilkas, false); });
        party.Until([&] { return guest.IsActorDisabled(kVilkas); }, "the guest's Vilkas is disabled");
        THEN("still a copy") CHECK(guest.IsActorCopy(kVilkas));
        party.Agree("the games agree on the disabled Vilkas");
    }

    WHEN("the host's guard walks out of the loaded cells")
    {
        party.As(party.Host(), [&] { host.MoveActorTo(guard, kSinkhole, 0.f, 0.f, 0.f); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy of the guard is deleted");
        party.Agree("the games agree on the actors left");
    }
}

SCENARIO("A guest joins while the party is elsewhere: it is brought there and gets every copy", "[copies]")
{
    Party party(1, PlacedNpcs, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    // The host's game in the sinkhole with Aela and a guard there, the guard dead; the guest's game in QASmoke.
    party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {10.f, 10.f, 0.f}); });
    uint32_t guard = 0;
    party.As(party.Host(), [&] {
        guard = host.LoadNpc(kGuard, kSinkhole, {40.f, -40.f, 0.f});
        host.Hit(guard, 200.f);
    });
    party.Until([&] { return OwnsHere(party.Host(), kAela) && OwnsHere(party.Host(), guard); },
                "the host's game owns Aela and the guard");
    REQUIRE(host.IsActorDead(guard));

    WHEN("the guest joins")
    {
        party.Join(party.Guest());
        party.Until([&] { return guest.GetPlayerParentCellId() == kSinkhole; }, "the guest is brought to the party",
                    [&] { return std::format("the guest's player in {:X}", guest.GetPlayerParentCellId()); });
        THEN("it has a copy of each of the host's actors, the guard dead")
        {
            party.Agree("the games agree on every actor");
            CHECK(guest.IsActorCopy(kAela));
            REQUIRE(guest.CopiesOf(kGuard).size() == 1);
            CHECK(guest.IsActorDead(guest.CopiesOf(kGuard)[0]));
        }
    }
}

namespace
{
constexpr uint32_t kTamriel = 0x0000003C;
constexpr uint32_t kFirstTamrielCell = 0x00020000; // the fake's: cells (-6..6, -6..6)
constexpr glm::vec3 kHostStands{100.f, 100.f, 0.f}; // in cell (0, 0)

void Outdoors(FakeEngine& aEngine)
{
    aEngine.Exterior(kTamriel, kFirstTamrielCell, 6);
    aEngine.PlayerStands(aEngine.CellAt(kTamriel, kHostStands), kHostStands);
}
} // namespace

SCENARIO("A guest joins outdoors four cells from the host, out of its grid: it is brought to the party", "[copies]")
{
    Party party(1, Outdoors, false);
    FakeEngine& guest = Party::Engine(party.Guest());

    const glm::vec3 cFar{4 * 4096.f + 100.f, 100.f, 0.f}; // in cell (4, 0)
    party.As(party.Guest(), [&] { guest.PlayerStands(guest.CellAt(kTamriel, cFar), cFar); });
    party.Join(party.Guest());

    const auto cFromHost = [&] {
        const auto& cPlayer = guest.Actors.at(FakeEngine::kPlayer);
        return glm::distance(glm::vec3{cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]}, kHostStands);
    };
    party.Until([&] { return cFromHost() < 64.f; }, "the guest is brought to the host's player",
                [&] { return std::format("the guest's player {} units from the host's", cFromHost()); });
    party.Agree("the games agree on the players");
}
