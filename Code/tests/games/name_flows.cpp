// Every name in a session on every game: players' copies, the party's lists, NPC copies (docs/re/systems/names.md).
#include "Party.h"
#include "Fixtures.h"
#include <Events/GameLoadedEvent.h>
#include <Events/MountEvent.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/Relayed.h>
#include <algorithm>
#include <Services/SessionService.h>
#include <Services/TransportService.h>

namespace
{
// This game's copy of that player's character, and what it shows.
std::string CopyName(World& aGame, uint32_t aPlayerId)
{
    for (auto&& [cEntity, cPlayer, cForm, cCopy] : aGame.view<PlayerComponent, FormIdComponent, RemoteComponent>().each())
        if (cPlayer.Id == aPlayerId)
            return Party::Engine(aGame).GetActorDisplayName(cForm.Id);
    return "<no copy>";
}

// Every game's copy and list name every other player as its character is named.
bool AllNamed(Party& aParty)
{
    std::vector<World*> games{&aParty.Host()};
    for (size_t i = 0; i + 1 < aParty.GameCount(); ++i)
        games.push_back(&aParty.Guest(i));
    for (World* pGame : games)
        for (World* pOther : games)
        {
            if (pGame == pOther)
                continue;
            const uint32_t cId = pOther->GetTransport().GetLocalPlayerId();
            const std::string cName = Party::Engine(*pOther).PlayerCharacterName();
            if (CopyName(*pGame, cId) != cName || pGame->GetSessionService().NameOf(cId) != cName)
                return false;
        }
    return true;
}

// The actor names a game was told (OnNotifyActorName's message).
struct NamesHeard
{
    explicit NamesHeard(World& aGame)
        : Connection(aGame.GetDispatcher().sink<NotifyActorName>().connect<&NamesHeard::On>(*this))
    {
    }
    void On(const NotifyActorName&) { ++Count; }
    int Count = 0;
    entt::scoped_connection Connection;
};

// The NPC spawns a game was told.
struct SpawnsHeard
{
    explicit SpawnsHeard(World& aGame)
        : Connection(aGame.GetDispatcher().sink<NotifyCharacterSpawn>().connect<&SpawnsHeard::On>(*this))
    {
    }
    void On(const NotifyCharacterSpawn& acSpawn) { Names.push_back(acSpawn.DisplayName); }
    [[nodiscard]] bool Named(const std::string& acName) const { return std::ranges::find(Names, acName) != Names.end(); }
    std::vector<std::string> Names;
    entt::scoped_connection Connection;
};

// The host's actor's copy on the guest, once it shows.
uint32_t CopyOf(Party& aParty, uint32_t aOwned)
{
    uint32_t sid = 0, copy = 0;
    aParty.Until([&] {
        for (auto&& [cEntity, cForm, cLocal] : aParty.Host().view<FormIdComponent, LocalComponent>().each())
            if (cForm.Id == aOwned)
                sid = cLocal.Id;
        for (auto&& [cEntity, cForm, cRemote] : aParty.Guest().view<FormIdComponent, RemoteComponent>().each())
            if (sid && cRemote.Id == sid)
                copy = cForm.Id;
        return copy != 0;
    }, "the guest shows the host's NPC");
    return copy;
}
} // namespace

SCENARIO("Every game names every player as its character is named", "[names][shared]")
{
    GIVEN("characters nobody named this session, whose saved change flags carry no name")
    {
        Party party(2, SmokeWorld);
        party.Until([&] { return AllNamed(party); }, "every copy and list names its player");
        party.Agree("the games agree on their players");
    }

    GIVEN("two players whose characters share a name")
    {
        Party party(2, [](FakeEngine& aEngine) {
            SmokeWorld(aEngine);
            aEngine.Player.Name = "Prisoner";
        });
        party.Until([&] { return AllNamed(party); }, "every copy and list names its player");
        CHECK(party.Host().GetSessionService().NameOf(party.Guest().GetTransport().GetLocalPlayerId()) == "Prisoner");
        CHECK(party.Host().GetSessionService().GetPlayers().size() == 2);
    }

    GIVEN("a name with characters beyond ASCII")
    {
        Party party(1, [](FakeEngine& aEngine) {
            SmokeWorld(aEngine);
            aEngine.Player.Name = "Bj\xC3\xB6rn";
        });
        party.Until([&] { return AllNamed(party); }, "every copy and list names its player");
    }
}

SCENARIO("A character renamed in the race menu has its new name on every game", "[names][shared]")
{
    Party party(2, SmokeWorld);
    party.Until([&] { return AllNamed(party); }, "every copy and list names its player");
    auto rename = [&](World& aGame, const char* acpName) {
        party.As(aGame, [&] { Party::Engine(aGame).PlayerUsesRaceMenu([&] { Party::Engine(aGame).PlayerNames(acpName); }); });
        party.Until([&] { return AllNamed(party); }, std::string("everyone calls the player ") + acpName);
    };
    WHEN("the guest renames its character")
    {
        // A save the host makes right after must keep the new name, even just after the last record was sent.
        const std::string cKey = party.Guest().ctx().at<JoinService>().PlayerKey();
        for (const char* cpName : {"Rinara", "Rinara Again"})
        {
            rename(party.Guest(), cpName);
            int frames = 0;
            party.Until([&] {
                ++frames;
                const auto* cpKept = party.Host().ctx().at<SaveService>().GetRecords().Find(cKey);
                return cpKept && cpKept->Name == cpName;
            }, std::string("the host keeps the name: ") + cpName);
            INFO(cpName);
            CHECK(frames < 60); // GuestRecord's usual wait is 5 s of game time, far more frames
        }
    }
    WHEN("the host renames its character") { rename(party.Host(), "Rinara"); }
}

SCENARIO("A host's load of another character renames it on every game", "[names]")
{
    Party party(1, SmokeWorld);
    party.Until([&] { return AllNamed(party); }, "every copy and list names its player");
    auto& host = Party::Engine(party.Host());
    party.As(party.Host(), [&] {
        host.Player.Name = "Loaded";
        party.Host().GetRunner().Trigger(GameLoadedEvent{});
    });
    party.Until([&] { return party.Guest().GetSessionService().NameOf(party.Host().GetTransport().GetLocalPlayerId()) == "Loaded"; },
                "the guest's list follows the host's load");
}

SCENARIO("An NPC copy shows its owner's name, whatever gave it that name", "[names][copies]")
{
    constexpr uint32_t cNpc = 0xD8C56;
    constexpr uint32_t cBase = 0x3DEE0;
    Party party(1, [&](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.Npcs[cBase].Name = "Whiterun Guard";
        aEngine.Npcs[cBase].Race = 0x13746;
        aEngine.PlaceNpc(cNpc, cBase, 0x31D2E, {});
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    const uint32_t cCopy = CopyOf(party, cNpc);
    CHECK(guest.GetActorDisplayName(cCopy) == "Whiterun Guard");

    WHEN("a quest alias names the NPC on the host, then lets it go")
    {
        party.As(party.Host(), [&] { host.AliasNames(cNpc, "Suspicious Guard"); });
        party.Until([&] { return guest.GetActorDisplayName(cCopy) == "Suspicious Guard"; }, "the copy takes the alias's name");
        party.As(party.Host(), [&] { host.AliasNames(cNpc, ""); });
        party.Until([&] { return guest.GetActorDisplayName(cCopy) == "Whiterun Guard"; }, "the copy goes back to its base's name");
        party.Agree("the games agree on the NPC");
        CHECK(guest.Npcs.at(cBase).Name == "Whiterun Guard"); // the name stays on the reference
    }
}

TEST_CASE("A rename while the guest's reference still loads reaches its copy", "[names][copies]")
{
    constexpr uint32_t cNpc = 0xD8C56;
    constexpr uint32_t cBase = 0x3DEE0;
    Party party(1, [&](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.Npcs[cBase].Name = "Whiterun Guard";
        aEngine.Npcs[cBase].Race = 0x13746;
        aEngine.PlaceNpc(cNpc, cBase, 0x31D2E, {});
        if (aEngine.Name != "Dovahkiin")
        {
            aEngine.Actors.at(cNpc).Initializing = true;
            aEngine.Actors.at(cNpc).Loaded = false;
        }
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    SpawnsHeard heardSpawn(party.Guest());
    // The first spawn waits on the reference.
    party.Until([&] { return heardSpawn.Named("Whiterun Guard"); }, "the guest hears of the NPC while its reference loads");
    NamesHeard heard(party.Guest());
    party.As(party.Host(), [&] { host.AliasNames(cNpc, "Suspicious Guard"); });
    party.Until([&] { return heard.Count > 0; }, "the guest hears the name before its reference has loaded");
    party.As(party.Guest(), [&] { guest.FinishLoading(cNpc); });
    party.Until([&] { return guest.IsActorCopy(cNpc); }, "the reference becomes the copy");
    CHECK(guest.GetActorDisplayName(cNpc) == "Suspicious Guard");
}

TEST_CASE("The host's quest names a horse its guest rider owns", "[names][mounts]")
{
    constexpr uint32_t cCell = 0x31D2E;
    constexpr uint32_t cHorseBase = 0x109E3D;
    Party party(2, [&](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = cCell;
        aEngine.LoadCell(cCell);
        aEngine.Npcs[cHorseBase].Name = "Horse";
    });
    party.Agree("all three players and their copies are present");
    auto& host = Party::Engine(party.Host());
    auto& rider = Party::Engine(party.Guest());
    auto& watcher = Party::Engine(party.Guest(1));
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(cHorseBase, cCell); });
    party.Until([&] { return rider.CopiesOf(cHorseBase).size() == 1 && watcher.CopiesOf(cHorseBase).size() == 1; }, "both guests have the horse");
    const uint32_t cHorse = rider.CopiesOf(cHorseBase)[0];
    party.As(party.Guest(), [&] {
        rider.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return host.IsActorCopy(horse); }, "the rider owns the horse");
    const auto cNamed = [&](const char* acpName) {
        return rider.GetActorDisplayName(cHorse) == acpName && watcher.GetActorDisplayName(watcher.CopiesOf(cHorseBase)[0]) == acpName;
    };
    party.As(party.Host(), [&] { host.AliasNames(horse, "Frost"); });
    party.Until([&] { return cNamed("Frost"); }, "the rider's horse and the other guest's copy take the name");
    party.As(party.Host(), [&] { host.AliasNames(horse, ""); });
    party.Until([&] { return cNamed("Horse"); }, "and lose it");
}
