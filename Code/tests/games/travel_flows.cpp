// The party tether across games (docs/design/host-authority-migration.md, "Party tether"; copy-contract.md):
// a player who travels by itself leads, everyone else follows; the players' copies travel with the party, the
// NPCs' stay with their cells.
#include "Party.h"
#include "Fixtures.h"
#include "FakeDialogue.h"

#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/RemoteComponent.h>
#include <Components/WaitingFor3D.h>
#include <Components/PlayerComponent.h>
#include <Components/SpawnGraphStateComponent.h>
#include <Components/OwnerPlaceComponent.h>
#include <Events/FastTravelEvent.h>
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/PartySummonCommandEvent.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyReferencesMove.h>
#include <Services/CharacterService.h>

#include <chrono>
#include <format>
#include <thread>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // where everyone starts
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kVilkas = 0x0001A695; // an NPC placed in kSmoke (the same reference in every game)

// The cells each game sees the party in: its own player's, and each player copy's.
std::vector<std::string> PartyApart(Party& aParty, size_t aGames)
{
    std::vector<std::string> apart;
    for (size_t i = 0; i < aGames; ++i)
    {
        const FakeEngine& cEngine = Party::Engine(i == 0 ? aParty.Host() : aParty.Guest(i - 1));
        const uint32_t cCell = cEngine.Actors.at(FakeEngine::kPlayer).Cell;
        size_t players = 0;
        for (const auto& [cId, cActor] : cEngine.Actors)
        {
            if (!cActor.Player || cId == FakeEngine::kPlayer)
                continue;
            ++players;
            if (cActor.Cell != cCell)
                apart.push_back(std::format("{}: a player's copy {:X} is in cell {:X}, its player in {:X}",
                                            cEngine.Name, cId, cActor.Cell, cCell));
        }
        if (players != aGames - 1)
            apart.push_back(std::format("{}: {} players' copies, not {}", cEngine.Name, players, aGames - 1));
    }
    return apart;
}

std::string Why(Party& aParty, size_t aGames)
{
    std::string why;
    for (size_t i = 0; i < aGames; ++i)
    {
        const FakeEngine& cEngine = Party::Engine(i == 0 ? aParty.Host() : aParty.Guest(i - 1));
        why += std::format("{} in {:X}; ", cEngine.Name, cEngine.Actors.at(FakeEngine::kPlayer).Cell);
    }
    for (const auto& cApart : PartyApart(aParty, aGames))
        why += cApart + "; ";
    return why;
}

bool Together(Party& aParty, size_t aGames, uint32_t aCell)
{
    for (size_t i = 0; i < aGames; ++i)
        if (Party::Engine(i == 0 ? aParty.Host() : aParty.Guest(i - 1)).Actors.at(FakeEngine::kPlayer).Cell != aCell)
            return false;
    return PartyApart(aParty, aGames).empty();
}

void CheckTogether(Party& aParty, size_t aGames)
{
    for (const auto& cApart : PartyApart(aParty, aGames))
        UNSCOPED_INFO(cApart);
    CHECK(PartyApart(aParty, aGames).empty());
}
} // namespace

SCENARIO("The host travels: the guest follows, and the players' copies come along", "[travel]")
{
    Party party(1, SmokeWorld);
    party.Until([&] { return Together(party, 2, kSmoke); }, "the party is together in QASmoke");

    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSinkhole, {100.f, 200.f, 0.f}); });
    party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                [&] { return Why(party, 2); });
    THEN("the guest stands where the host arrived")
    {
        const auto& cGuest = Party::Engine(party.Guest()).Actors.at(FakeEngine::kPlayer);
        CHECK(cGuest.Position[0] == 100.f);
        CHECK(cGuest.Position[1] == 200.f);
    }
    CheckTogether(party, 2);
    party.Agree("the games agree on the players");
}

SCENARIO("A guest travels: the host and the other guest follow", "[travel]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party is together in QASmoke");

    party.As(party.Guest(0), [&] { Party::Engine(party.Guest(0)).PlayerTravels(kSinkhole, {5.f, 5.f, 0.f}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the party is together in the sinkhole",
                [&] { return Why(party, 3); });
    CheckTogether(party, 3);
    party.Agree("the three games agree on the players");
}

SCENARIO("An NPC stays with its cell when the party leaves it", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return !guest.CopiesOf(kGuard).empty(); }, "the guest has the host's guard");

    WHEN("the host leads the party away")
    {
        party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {}); });
        party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                    [&] { return Why(party, 2); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy of the guard goes");
        party.Agree("the games agree on the actors in the sinkhole");

        AND_WHEN("an NPC loads where the party is now")
        {
            party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSinkhole); });
            party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest gets a copy of it");
            THEN("the copy is in the sinkhole")
            {
                CHECK(guest.Actors.at(guest.CopiesOf(kGuard)[0]).Cell == kSinkhole);
                party.Agree("the games agree on the new guard");
            }
        }
    }
}

SCENARIO("A placed NPC stays with its cell when the party leaves it, and is the host's copy again on its return",
         "[travel]")
{
    Party party(1, [](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.PlaceNpc(kVilkas, 0x0001A694, kSmoke, {100.f, 0.f, 0.f});
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSmoke, {10.f, 0.f, 0.f}); });
    party.Until([&] { return guest.IsActorCopy(kVilkas) && guest.CopiesOf(kGuard).size() == 1; },
                "the guest's Vilkas is the host's copy, and it has the host's guard");
    party.Agree("the games agree on Vilkas and the guard");

    WHEN("the host leads the party away")
    {
        party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {}); });
        party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                    [&] { return Why(party, 2); });
        party.Until([&] { return guest.CopiesOf(kGuard).empty(); }, "the guest's copy of the guard goes");
        THEN("the guest's Vilkas stays in QASmoke, held as a copy")
        {
            CHECK(guest.Actors.at(kVilkas).Cell == kSmoke);
            CHECK(guest.IsActorCopy(kVilkas));
            party.Agree("the games agree on the actors");
        }

        AND_WHEN("the host leads it back")
        {
            party.As(party.Host(), [&] { host.PlayerTravels(kSmoke, {}); });
            party.Until([&] { return Together(party, 2, kSmoke); }, "the party is back in QASmoke",
                        [&] { return Why(party, 2); });
            party.Until(
                [&] {
                    for (const auto cEntity : party.Guest().view<FormIdComponent, RemoteComponent>())
                        if (party.Guest().get<FormIdComponent>(cEntity).Id == kVilkas)
                            return true;
                    return false;
                },
                "the guest's Vilkas is the host's copy again");
            party.Agree("the games agree on Vilkas back");
        }
    }

    WHEN("the guest leads the party away: its arrival drops its copies before the host's game leaves")
    {
        const size_t cGuards = guest.Spawned[kGuard];
        party.As(party.Guest(), [&] { guest.PlayerTravels(kSinkhole, {}); });
        party.Until([&] { return Together(party, 2, kSinkhole); }, "the party is together in the sinkhole",
                    [&] { return Why(party, 2); });
        THEN("the guest's Vilkas stays in QASmoke, held as a copy, and nothing the host left is sent it")
        {
            party.Agree("the games agree on the actors");
            CHECK(guest.Actors.at(kVilkas).Cell == kSmoke);
            CHECK(guest.IsActorCopy(kVilkas));
            CHECK(guest.Spawned[kGuard] == cGuards);
        }
    }
}

SCENARIO("An NPC the host's game loads as a following guest's arrival reaches it: the guest gets its copy", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the games agree on the players");

    // The host leads; the guest follows and its arrival is on its way to the host (sent on its next frame).
    party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {}); });
    party.Step(party.Host());
    party.Until([&] { return guest.GetPlayerParentCellId() == kSinkhole; }, "the guest follows to the sinkhole", {},
                &party.Guest());
    party.Step(party.Guest());

    WHEN("in the host's next frame its game announces an NPC, then its rules take the guest's arrival")
    {
        // The host's game owns the guard from its announcement; its rules take the guest's arrival after it.
        party.As(party.Host(), [&] { host.LoadNpc(kGuard, kSinkhole, {20.f, 0.f, 0.f}); });
        party.Step(party.Host());
        party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest gets a copy of the guard");
        party.Agree("the games agree on the guard");
    }
}

SCENARIO("A copy that travels stands at its owner's new place, never racing toward its old one", "[travel][copy-step]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the games agree on the players");
    const auto cCopyOf = [&] {
        for (const uint32_t cId : guest.CopiesOf())
            if (guest.Actors.at(cId).Player)
                return cId;
        return 0u;
    };
    // Stepped to where the host stands, far from where it goes (copies-keep-place-on-arrival).
    // Far, and nearer than one frame's fastest step.
    const glm::vec2 cFrom = GENERATE(glm::vec2(3000.f, 0.f), glm::vec2(-930.f, 500.f));
    host.Actors.at(FakeEngine::kPlayer).Position[0] = cFrom.x;
    host.Actors.at(FakeEngine::kPlayer).Position[1] = cFrom.y;
    party.Until([&] { return guest.Actors.at(cCopyOf()).Stepped && std::abs(guest.Actors.at(cCopyOf()).Position[0] - cFrom.x) < 5.f; },
                "the guest's copy is stepped where the host stands");

    const glm::vec3 cSpot{-1000.f, 500.f, 0.f};
    party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, cSpot); });
    float worst = 0.f;
    size_t seen = 0;
    const auto cWatch = [&] {
        const auto& cCopy = guest.Actors.at(cCopyOf());
        if (cCopy.Cell != kSinkhole)
            return;
        worst = std::max(worst, glm::distance(glm::vec2(cCopy.Position[0], cCopy.Position[1]), glm::vec2(cSpot)));
        ++seen;
    };
    party.Until([&] { cWatch(); return Together(party, 2, kSinkhole); }, "the party is in the sinkhole");
    for (int i = 0; i < 60; ++i)
    {
        party.Frame();
        cWatch();
    }
    INFO(seen << " frames in the new cell, at worst " << worst << " units off");
    CHECK(seen > 0);
    CHECK(worst < 5.f);
}

SCENARIO("A snapshot older than a copy's jump never takes it back", "[travel][copy-step]")
{
    // Movement goes unreliably on its own channel, a jump or a spawn reliably: one sent before can arrive after.
    enum class Late { kNone, kSameCell, kTwo, kOldCell, kAfterSpawn, kOlderSpawn };
    const Late cLate = GENERATE(Late::kNone, Late::kSameCell, Late::kTwo, Late::kOldCell, Late::kAfterSpawn, Late::kOlderSpawn);
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the games agree on the players");
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(kGuard, kSmoke, {0.f, 0.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && guest.Actors.at(guest.CopiesOf(kGuard)[0]).Stepped; },
                "the guest steps the guard's copy");
    const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
    uint32_t id = 0;
    for (auto&& [cEntity, cForm, cLocal] : party.Host().view<FormIdComponent, LocalComponent>().each())
        if (cForm.Id == npc)
            id = cLocal.Id;
    REQUIRE(id != 0);
    entt::entity entity = entt::null;
    for (auto&& [cEntity, cRemote] : party.Guest().view<RemoteComponent>().each())
        if (cRemote.Id == id)
            entity = cEntity;
    const GameId cCell = party.Guest().get<OwnerPlaceComponent>(entity).CellId;
    GameId oldCell = cCell;
    oldCell.BaseId = kSinkhole;

    const auto cSpawnNow = [&] {
        std::vector<NotifyCharacterSpawn> spawns;
        party.As(party.Host(), [&] { spawns = party.Host().GetCharacterService().BuildSpawnRequests(0); });
        const auto cSpawn = std::find_if(spawns.begin(), spawns.end(), [&](const auto& acSpawn) { return acSpawn.SessionId == id; });
        REQUIRE(cSpawn != spawns.end());
        return *cSpawn;
    };
    const auto cGets = [&](const NotifyCharacterSpawn& acSpawn) {
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(acSpawn); });
    };
    NotifyCharacterSpawn older = cSpawnNow();
    party.Frame();
    constexpr float cSpot = 1500.f;
    host.Actors.at(npc).Position[0] = cSpot;
    const uint64_t cNow = party.Host().GetFrameTick();
    older.Tick = cNow - 20; // frames can share a tick
    ActionEvent gesture;
    gesture.Tick = cNow - 40;
    gesture.ActionId = 0x489F1;
    gesture.IdleId = 0xF779D;
    gesture.EventName = "MotionDrivenIdle";
    const auto cArrives = [&](uint64_t aTick, float aX, bool aTeleported, const GameId& acCell, bool aActs) {
        NotifyReferencesMove message;
        message.Tick = aTick;
        auto& update = message.Updates[id];
        update.UpdatedMovement.Position = glm::vec3(aX, 0.f, 0.f);
        update.UpdatedMovement.Teleported = aTeleported;
        update.UpdatedMovement.OnGround = true;
        update.UpdatedMovement.CellId = acCell;
        if (aActs)
            update.ActionEvents.push_back(gesture);
        party.As(party.Guest(), [&] { party.Guest().GetCharacterService().OnReferencesMoveRequest(message); });
    };
    if (cLate == Late::kAfterSpawn)
        cGets(cSpawnNow());
    else
        cArrives(cNow, cSpot, true, cCell, false);
    party.Frame();
    size_t back = 0;
    const auto cOnSpot = [&] { return std::abs(guest.Actors.at(cCopy).Position[0] - cSpot) < 5.f; };
    CHECK(cOnSpot()); // one place is enough to stand there
    const auto cState = [&] { return party.Guest().get<OwnerPlaceComponent>(entity).CellId; };
    if (cLate == Late::kTwo)
        cArrives(cNow - 80, -30.f, false, cCell, false);
    if (cLate == Late::kOlderSpawn)
        cGets(older);
    else if (cLate != Late::kNone)
        cArrives(cNow - 40, 0.f, false, cLate == Late::kOldCell ? oldCell : cCell, true);
    CHECK(cState() == cCell);

    // Queued before the rest: a place seen on the frame after the late one.
    const auto cUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (std::chrono::steady_clock::now() < cUntil)
    {
        party.Frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        back += !cOnSpot();
    }
    INFO(back << " frames off its spot");
    CHECK(back == 0);
    if (cLate != Late::kNone && cLate != Late::kOlderSpawn)
        CHECK(guest.GetActorLatestAnimation(cCopy).IdleId == gesture.IdleId);
}

SCENARIO("An actor handed over stands at its new owner's first snapshot, never at the origin", "[travel][copy-step]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    party.Agree("the games agree on the players");
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(kGuard, kSmoke, {900.f, 700.f, 0.f}); });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    entt::entity entity = entt::null;
    uint32_t id = 0;
    for (auto&& [cEntity, cForm, cLocal] : party.Host().view<FormIdComponent, LocalComponent>().each())
        if (cForm.Id == npc)
        {
            entity = cEntity;
            id = cLocal.Id;
        }
    REQUIRE(id != 0);
    party.As(party.Host(), [&] { party.Host().GetCharacterService().GiveUpOwnership(entity, id, npc); });
    party.Step(party.Host());
    CHECK(host.Actors.at(npc).Position[0] == 900.f); // nothing said yet
    NotifyReferencesMove message;
    message.Tick = party.Host().GetFrameTick();
    message.Updates[id].UpdatedMovement.Position = glm::vec3(950.f, 700.f, 0.f);
    message.Updates[id].UpdatedMovement.CellId = GameId{0, kSmoke};
    message.Updates[id].UpdatedMovement.OnGround = true;
    party.As(party.Host(), [&] { party.Host().GetCharacterService().OnReferencesMoveRequest(message); });
    party.Step(party.Host());
    CHECK(std::abs(host.Actors.at(npc).Position[0] - 950.f) < 1.f);
    CHECK(std::abs(host.Actors.at(npc).Position[1] - 700.f) < 1.f);
}

SCENARIO("A spawn built from a copy carries its owner's newest place, cell and tick", "[travel][copy-step]")
{
    Party party(1, SmokeWorld);
    party.Agree("the games agree on the players");
    for (int i = 0; i < 10; ++i)
        party.Frame();
    uint32_t id = 0;
    for (auto&& [cEntity, cRemote, cPlayer] : party.Host().view<RemoteComponent, PlayerComponent>().each())
        id = cRemote.Id;
    REQUIRE(id != 0);
    NotifyReferencesMove message;
    message.Tick = party.Host().GetFrameTick() + 5;
    message.Updates[id].UpdatedMovement.Position = glm::vec3(1500.f, 20.f, 0.f);
    message.Updates[id].UpdatedMovement.CellId = GameId{0, kSinkhole};
    message.Updates[id].UpdatedMovement.Teleported = true;
    NotifyCharacterSpawn spawn;
    party.As(party.Host(), [&] {
        party.Host().GetCharacterService().OnReferencesMoveRequest(message);
        REQUIRE(party.Host().GetCharacterService().ReadCopy(id, spawn));
    });
    CHECK(spawn.Tick == message.Tick);
    CHECK(spawn.CellId == GameId{0, kSinkhole});
    CHECK(glm::distance(glm::vec3(spawn.Position), glm::vec3(1500.f, 20.f, 0.f)) < 1.f);
}

SCENARIO("A follower landing on a guest's arrival spot does not shove the guest", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the games agree on the players");
    const glm::vec3 cSpot{300.f, 300.f, 0.f};

    // The guest leads in through a door; the host's follower lands right where the guest stands
    // (follower-lands-on-guest: her copy's capsule never yields).
    party.As(party.Guest(), [&] { guest.PlayerTravels(kSinkhole, cSpot); });
    party.Until([&] { return host.GetPlayerParentCellId() == kSinkhole; }, "the host follows to the sinkhole");
    uint32_t follower = 0;
    party.As(party.Host(), [&] { follower = host.LoadNpc(kGuard, kSinkhole, cSpot); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && guest.Actors.at(guest.CopiesOf(kGuard)[0]).Stepped; },
                "the guest's copy of the follower is stepped on the spot");
    for (int i = 0; i < 30; ++i)
        party.Frame();

    THEN("the guest stays on its spot, passing through the follower's copy")
    {
        const auto& cGuest = guest.Actors.at(FakeEngine::kPlayer);
        INFO("shoves " << guest.PlayerShoves << ", passing " << guest.PassingThrough.size());
        CHECK(guest.PlayerShoves == 0);
        CHECK(glm::distance(glm::vec2(cGuest.Position[0], cGuest.Position[1]), glm::vec2(cSpot)) < 1.f);
        CHECK(std::ranges::find(guest.PassingThrough, guest.CopiesOf(kGuard)[0]) != guest.PassingThrough.end());
    }
    (void)follower;
}

SCENARIO("A guest's native fast travel advances the party calendar once across midnight", "[travel][world]")
{
    Party party(2, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    host.Calendar.Time = 23.f;
    host.Calendar.DaysPassed = 230;
    host.Calendar.Day = 17;
    party.Until([&] { return Party::Engine(party.Guest()).Calendar.DaysPassed == 230; }, "the guests have the host's date");
    party.As(party.Guest(), [&] {
        party.Guest().GetRunner().Trigger(FastTravelEvent{3.f});
        Party::Engine(party.Guest()).PlayerTravels(kSinkhole, {});
    });
    party.Until([&] { return host.Calendar.DaysPassed == 231; }, "the guest's three-hour trip advances the host across midnight");
    CHECK(host.Calendar.Time >= 2.f);
    CHECK(host.Calendar.Time < 2.05f);
    CHECK(host.Calendar.Day == 18);
    party.Until([&] { return Together(party, 3, kSinkhole) &&
                           Party::Engine(party.Guest()).Calendar.DaysPassed == 231 &&
                           Party::Engine(party.Guest(1)).Calendar.DaysPassed == 231; }, "the party and its calendar arrive together");
    party.Agree("the three games agree after fast travel");
}

SCENARIO("A queued guest travel starts the other games before its own arrival", "[travel]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party starts together");
    const auto cTravels = Party::Engine(party.Guest()).Travels;
    party.As(party.Guest(), [&] {
        party.Guest().GetRunner().Trigger(PlayerTravelQueuedEvent{0, kSinkhole, {30.f, 40.f, 0.f}, 1.f});
    });
    party.Until([&] { return Party::Engine(party.Host()).GetPlayerParentCellId() == kSinkhole &&
                           Party::Engine(party.Guest(1)).GetPlayerParentCellId() == kSinkhole; },
                "the host and the other guest begin their moves before the leader arrives");
    CHECK(Party::Engine(party.Guest()).Travels == cTravels);
    CHECK(Party::Engine(party.Guest()).GetPlayerParentCellId() == kSmoke);
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).PlayerTravels(kSinkhole, {30.f, 40.f, 0.f}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the party converges on the actual arrival");
    party.Agree("the copies converge too");
}

SCENARIO("A dead following player travels with the party and stays dead", "[travel][death]")
{
    Party party(2, SmokeWorld);
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return Together(party, 3, kSmoke); }, "the three players start together");
    party.Until([&] {
        for (World* pGame : {&party.Host(), &party.Guest(), &party.Guest(1)})
            for (const auto cEntity : pGame->view<RemoteComponent, PlayerComponent, FormIdComponent>())
                if (pGame->any_of<WaitingFor3D, SpawnGraphStateComponent>(cEntity) ||
                    !Party::Engine(*pGame).ActorHas3D(pGame->get<FormIdComponent>(cEntity).Id))
                    return false;
        return true;
    }, "the initial player copies finish loading before the trip");
    party.As(party.Host(), [&] {
        auto& host = Party::Engine(party.Host());
        const uint32_t cEnemy = host.LoadNpc(kGuard, kSmoke);
        host.Fights(FakeEngine::kPlayer, cEnemy);
    });
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    REQUIRE(guest.IsActorDead(FakeEngine::kPlayer));
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSinkhole, {}); });
    party.Until([&] { return Together(party, 3, kSinkhole); }, "the dead guest follows the living party", [&] { return Why(party, 3); });
    CHECK(guest.IsActorDead(FakeEngine::kPlayer));
    CHECK_FALSE(guest.Reloaded);
    party.Agree("the copies agree on the travelling dead player");
}

SCENARIO("Party travel closes a following guest's dialogue and takes both guests along", "[travel][dialogue]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the three players start together");
    uint32_t speaker = 0;
    party.As(party.Host(), [&] { speaker = Party::Engine(party.Host()).LoadNpc(kGuard, kSmoke); });
    party.Until([&] { return !Party::Engine(party.Guest()).CopiesOf(kGuard).empty(); }, "the guest can talk to the guard");
    auto& dialogue = party.Guest().ctx().at<FakeDialogue&>();
    party.As(party.Guest(), [&] { dialogue.PlayerTalksTo(Party::Engine(party.Guest()).CopiesOf(kGuard)[0]); });
    party.Until([&] { return dialogue.MenuOpen; }, "the guest's dialogue opens");
    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSinkhole, {}); });
    party.Until([&] { return Together(party, 3, kSinkhole) && !dialogue.MenuOpen; }, "the dialogue ends as its player follows", [&] { return Why(party, 3) + (dialogue.MenuOpen ? "menu still open" : "menu closed"); });
    party.Agree("the three games agree after leaving the dialogue");
}

SCENARIO("/tpa from another cell brings the party there", "[travel]")
{
    Party party(1, SmokeWorld);
    party.Until([&] { return Together(party, 2, kSmoke); }, "the party starts together");
    // The guest is in the sinkhole alone (the tether missed it: a move before joining leads nobody).
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Guest(), [&] { guest.MoveActorTo(FakeEngine::kPlayer, kSinkhole, 50.f, 60.f, 0.f); guest.Teleported = false; });

    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(PartySummonCommandEvent{}); });
    party.Until([&] { return Together(party, 2, kSinkhole); }, "the host comes to the sinkhole",
                [&] { return Why(party, 2); });
    party.Agree("the games agree on the players after /tpa");
}

SCENARIO("/tpa brings every other player onto the summoner's spot, even within its cell; all pass through there", "[travel]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party starts together");
    FakeEngine& summoner = Party::Engine(party.Guest(0));
    // Everyone walks off the join's arrival spot, the summoner too: the tether sees the others in the cell already.
    const glm::vec3 cAt{500.f, 500.f, 0.f};
    party.As(party.Guest(0), [&] { std::copy_n(&cAt.x, 3, summoner.Actors.at(FakeEngine::kPlayer).Position); });
    for (World* pGame : {&party.Host(), &party.Guest(1)})
        party.As(*pGame, [&] {
            auto& player = Party::Engine(*pGame).Actors.at(FakeEngine::kPlayer);
            player.Position[0] = 3000.f;
            player.Position[1] = -2000.f;
        });
    const auto cGames = {&party.Host(), &party.Guest(0), &party.Guest(1)};
    party.Until([&] { return std::ranges::all_of(cGames, [](World* pGame) { return Party::Engine(*pGame).PassingThrough.empty(); }); },
                "the join's arrival stack is walked off in every game");

    party.As(party.Guest(0), [&] { party.Guest(0).GetDispatcher().trigger(PartySummonCommandEvent{}); });
    const auto cOnSpot = [&](World& aGame) {
        const auto& cPlayer = Party::Engine(aGame).Actors.at(FakeEngine::kPlayer);
        return glm::distance(glm::vec2(cPlayer.Position[0], cPlayer.Position[1]), glm::vec2(cAt)) < 1.f;
    };
    party.Until([&] { return cOnSpot(party.Host()) && cOnSpot(party.Guest(1)); },
                "the host and the other guest land on the summoner's spot", [&] { return Why(party, 3); });
    // Each game: its player and both players' copies pass through each other on the spot.
    const auto cAllPassing = [](const FakeEngine& acEngine) {
        size_t players = 0;
        for (const auto& [cId, cActor] : acEngine.Actors)
            if (cActor.Player && std::ranges::find(acEngine.PassingThrough, cId) != acEngine.PassingThrough.end())
                ++players;
        return players == 3;
    };
    party.Until([&] { return std::ranges::all_of(cGames, [&](World* pGame) { return cAllPassing(Party::Engine(*pGame)); }); },
                "every game lets the three players pass through each other on the summoner's spot", [&] {
                    std::string why;
                    for (World* pGame : cGames)
                        why += std::format("{} passes {}; ", Party::Engine(*pGame).Name, Party::Engine(*pGame).PassingThrough.size());
                    return why + Why(party, 3);
                });
    CHECK(Party::Engine(party.Host()).GetPlayerParentCellId() == kSmoke);
    CHECK(cOnSpot(party.Guest(0)));
    CheckTogether(party, 3);
    party.Agree("the games agree on the players after /tpa");
}

namespace
{
bool StandsAt(const FakeEngine& acEngine, const glm::vec3& acSpot)
{
    const auto& cPlayer = acEngine.Actors.at(FakeEngine::kPlayer);
    return glm::distance(glm::vec2(cPlayer.Position[0], cPlayer.Position[1]), glm::vec2(acSpot)) < 1.f;
}
} // namespace

// Any teleport of a player brings the party, inside the cell too (docs/design/party-travel.md, "Every teleport").
SCENARIO("The host teleports inside its cell: the guest comes along", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return Together(party, 2, kSmoke); }, "the party is together in QASmoke");
    const size_t cTravels = guest.Travels;
    const glm::vec3 cSpot{2500.f, -1800.f, 0.f};

    party.As(party.Host(), [&] { Party::Engine(party.Host()).PlayerTravels(kSmoke, cSpot); });
    party.Until([&] { return StandsAt(guest, cSpot); }, "the guest stands where the host arrived",
                [&] { return Why(party, 2); });
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the guest teleported once, and the party is still together")
    {
        CHECK(guest.Travels == cTravels + 1);
        CHECK(Together(party, 2, kSmoke));
    }
    party.Agree("the games agree on the players");
}

SCENARIO("A guest's teleport inside the cell brings the host and the other guest", "[travel]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return Together(party, 3, kSmoke); }, "the party is together in QASmoke");
    const glm::vec3 cSpot{-900.f, 1200.f, 0.f};

    party.As(party.Guest(0), [&] { Party::Engine(party.Guest(0)).PlayerTravels(kSmoke, cSpot); });
    party.Until([&] { return StandsAt(Party::Engine(party.Host()), cSpot) && StandsAt(Party::Engine(party.Guest(1)), cSpot); },
                "the host and the other guest stand where the guest arrived", [&] { return Why(party, 3); });
    CheckTogether(party, 3);
    party.Agree("the three games agree on the players");
}

SCENARIO("Paying a fine: the host's release inside its worldspace brings the guest", "[travel][crime]")
{
    Party party(1, [](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.Exterior(0x3C, 0x10000, 2);
    });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    const glm::vec3 cArrest{100.f, 200.f, 0.f};
    party.As(party.Host(), [&] { host.PlayerTravels(0x10000, cArrest); });
    party.Until([&] { return guest.Actors.at(FakeEngine::kPlayer).WorldSpace == 0x3C && StandsAt(guest, cArrest); },
                "the party is outdoors together");

    // The release marker, a cell over in the same loaded grid (Riften, 2026-11-08: "already there").
    const glm::vec3 cRelease{4500.f, 300.f, 0.f};
    party.As(party.Host(), [&] { host.PlayerTravelsUnannounced(0x10000, cRelease); });
    party.Until([&] { return StandsAt(guest, cRelease); }, "the guest stands at the release marker with the host",
                [&] { return Why(party, 2); });
    party.Agree("the games agree on the players");
}

SCENARIO("A follower that walked off is not pulled back by its leader's late arrival", "[travel]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return Together(party, 2, kSmoke); }, "the party is together in QASmoke");
    const glm::vec3 cSpot{1500.f, 1500.f, 0.f};

    // The host's teleport is announced before it loads; the guest follows and walks off before the host arrives.
    party.As(party.Host(), [&] {
        party.Host().GetRunner().Trigger(PlayerTravelQueuedEvent{0, kSmoke, cSpot, 0.f});
    });
    party.Until([&] { return StandsAt(guest, cSpot); }, "the guest follows the announced teleport");
    for (int i = 0; i < 10; ++i)
        party.Frame();
    const size_t cTravels = guest.Travels;
    auto& guestPlayer = guest.Actors.at(FakeEngine::kPlayer);
    guestPlayer.Position[0] += 400.f;
    const glm::vec3 cWalked{guestPlayer.Position[0], guestPlayer.Position[1], guestPlayer.Position[2]};

    party.As(party.Host(), [&] { host.PlayerTravelsUnannounced(kSmoke, cSpot); }); // announced above
    for (int i = 0; i < 30; ++i)
        party.Frame();
    THEN("the host's arrival of the same trip moves the guest no more")
    {
        CHECK(guest.Travels == cTravels);
        CHECK(StandsAt(guest, cWalked));
    }
}
