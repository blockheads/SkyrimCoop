// Every name in a session on every game: players' copies, the party's lists, NPC copies (docs/re/systems/names.md).
#include "Party.h"
#include "Fixtures.h"
#include <Events/GameLoadedEvent.h>
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
    WHEN("the guest renames its character") { rename(party.Guest(), "Rinara"); }
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
