// Copies across games (docs/design/copy-contract.md): an NPC the host's game loads gets a copy on each guest's game;
// a guest's player gets a copy on the host's; each goes, or follows, when its owner's actor does. Each ends with the
// actor checksum (Party::Agree).
#include "Party.h"

#include <Components.h>
#include <Events/DialogueEvent.h>
#include <Events/PlayerLooksChangeEvent.h>

#include <ranges>

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

constexpr uint32_t kNordVampireRace = 0x00088794;
constexpr uint32_t kKhajiitRace = 0x00013745;
constexpr uint32_t kBretonRace = 0x00013741;

// That game's copy of the other game's player (a party of two), 0 while it has none.
uint32_t PlayerCopy(const FakeEngine& acEngine)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            return cId;
    return 0;
}

// The copies that game made, ever (each respawn makes a new one).
size_t CopiesMade(const FakeEngine& acEngine)
{
    return std::ranges::fold_left(acEngine.Spawned | std::views::values, size_t{0}, std::plus{});
}

// That game's copy of acOwner's player wears its looks: its base's save data, weight, and the tints FaceGenSystem
// paints on its head.
bool WearsLooks(World& aGame, const FakeEngine& acOwner)
{
    const FakeEngine& cEngine = Party::Engine(aGame);
    const uint32_t cCopy = PlayerCopy(cEngine);
    if (!cCopy)
        return false;
    const auto& cBase = cEngine.Npcs.at(cEngine.GetActorBaseFormId(cCopy));
    const FaceGenComponent* cpFace = nullptr;
    for (const auto cEntity : aGame.view<FormIdComponent, FaceGenComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == cCopy)
            cpFace = &aGame.get<FaceGenComponent>(cEntity);
    return cBase.Looks == acOwner.Player.Base && cBase.Weight == acOwner.Player.Weight && cpFace &&
           cpFace->FaceTints == acOwner.Player.FaceTints;
}

// A player's tints as the race menu leaves them: skin tone and a war paint.
Tints PaintedTints(uint32_t aPaint)
{
    Tints tints;
    tints.Entries.push_back({"Actors\\Character\\Character Assets\\TintMasks\\SkinTone.dds", 1.f, 0xC09070, 6});
    tints.Entries.push_back({"Actors\\Character\\Character Assets\\TintMasks\\FemaleHeadWarPaint_04.dds", 0.8f, aPaint, 3});
    return tints;
}

// The times a game's player's looks changed for its copies (PlayerService's PlayerLooksChangeEvent).
struct LooksChanges
{
    explicit LooksChanges(World& aGame)
        : Connection(aGame.GetDispatcher().sink<PlayerLooksChangeEvent>().connect<&LooksChanges::On>(*this))
    {
    }
    void On(const PlayerLooksChangeEvent&) { ++Count; }
    size_t Count = 0;
    entt::scoped_connection Connection;
};

// Each player in a look of their own: a Nord with a war paint, at Skyrim.esm's player's build.
void PaintedPlayers(FakeEngine& aEngine)
{
    Plugins(aEngine);
    aEngine.Player.Base = "race 13746";
    aEngine.Player.FaceTints = PaintedTints(0x202020);
    aEngine.Player.Weight = 100.f;
}

// Each player at Skyrim.esm's player's build (weight 100), past a new NPC's 50.
void HeavyPlayers(FakeEngine& aEngine)
{
    Plugins(aEngine);
    aEngine.Player.Weight = 100.f;
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

    WHEN("the host's guard goes (its cell unloads it) the frame the guest got its copy")
    {
        // The host's game owns it from its announcement: no frame waits for an answer.
        REQUIRE(OwnsHere(party.Host(), npc));
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
    Party party(1, HeavyPlayers);
    FakeEngine& host = Party::Engine(party.Host());

    const auto cPlayerCopies = [&] {
        size_t copies = 0;
        for (const uint32_t cId : host.CopiesOf())
            copies += host.Actors.at(cId).Player;
        return copies;
    };
    party.Until([&] { return cPlayerCopies() == 1; }, "the host's game makes a copy of the guest's player");
    party.Agree("each game has the other's player");
    THEN("each player's copy has its player's build (its weight, which the base's save data leaves out)")
    {
        FakeEngine& guest = Party::Engine(party.Guest());
        for (FakeEngine* pGame : {&host, &guest})
            for (const uint32_t cId : pGame->CopiesOf())
                if (pGame->Actors.at(cId).Player)
                    CHECK(pGame->GetActorBaseWeight(cId) == 100.f);
    }

    WHEN("the guest leaves")
    {
        party.Leave(party.Guest());
        party.Until([&] { return cPlayerCopies() == 0; }, "the host's copy of the guest's player goes");
    }
}

SCENARIO("A player's looks change: its copies are made again from them", "[copies]")
{
    Party party(1, PaintedPlayers);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return WearsLooks(party.Host(), guest) && WearsLooks(party.Guest(), host); },
                "each game's copy of the other's player wears its looks");
    party.Agree("each game has the other's player");

    WHEN("the guest's player catches vampirism: its race switches, its skin with it")
    {
        party.As(party.Guest(), [&] {
            guest.Player.FaceTints = PaintedTints(0x101010);
            guest.PlayerSwitchesRace(kNordVampireRace);
        });
        party.Until([&] { return WearsLooks(party.Host(), guest); }, "the host's copy is made again as a vampire, in its new skin");
        party.Agree("the games agree on the guest's player");
    }

    WHEN("the guest's player tries races in the race menu, then a new build and war paint, and closes it")
    {
        const size_t cMade = CopiesMade(host);
        const LooksChanges cChanges(party.Guest());
        party.As(party.Guest(), [&] {
            guest.PlayerUsesRaceMenu([&] {
                guest.PlayerSwitchesRace(kKhajiitRace);
                guest.PlayerSwitchesRace(kBretonRace);
                guest.Player.Weight = 0.f;
                guest.Player.FaceTints = PaintedTints(0x8000FF);
            });
        });
        party.Until([&] { return WearsLooks(party.Host(), guest); }, "the host's copy wears what the menu made");
        party.Agree("the games agree on the guest's player");
        THEN("the copy is made again once, when the menu closes, not for each race tried")
        {
            CHECK(cChanges.Count == 1);
            CHECK(CopiesMade(host) == cMade + 1);
        }
    }

    WHEN("the host's player switches race")
    {
        party.As(party.Host(), [&] { host.PlayerSwitchesRace(kKhajiitRace); });
        party.Until([&] { return WearsLooks(party.Guest(), host); }, "the guest's copy of the host's player is made again as a Khajiit");
        party.Agree("the games agree on the host's player");
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

SCENARIO("The host's NPC speaks while the guest's copy has no 3D yet: the copy stays silent", "[copies]")
{
    Party party(1, PlacedNpcs);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {-30.f, 10.f, 0.f}); });
    party.Until([&] { return guest.IsActorCopy(kVilkas) && guest.CopiesOf(kGuard).size() == 1; },
                "the guest's Vilkas is the host's copy, and it has the host's guard");

    // The guest's game still loading him (2026-10-23, a bandit after a teleport): Actor::SpeakSound crashed it.
    guest.Actors.at(kVilkas).Loaded = false;
    party.As(party.Host(), [&] {
        party.Host().GetDispatcher().trigger(DialogueEvent(kVilkas, "vilkas.fuz"));
        party.Host().GetDispatcher().trigger(DialogueEvent(guard, "guard.fuz"));
    });
    party.Until([&] { return !guest.Lines.empty() && guest.Lines.back().second == "guard.fuz"; },
                "the guest's guard says the line said after Vilkas's");
    CHECK(guest.SpokeWithout3D == 0);
    CHECK(guest.Lines.size() == 1);
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
