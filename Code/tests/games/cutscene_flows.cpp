// Cutscenes for the whole party across games (docs/design/cutscenes.md): a world script's controls reach every game,
// and a leaver's cutscene gives back what it held.
#include "Party.h"
#include <Messages/Relayed.h>

using namespace PartyRules;

SCENARIO("Another guest's AI state cannot interrupt the host's scene", "[cutscene][scene]")
{
    Party party(2);
    World& host = party.Host();
    World& guest = party.Guest();
    const auto cPlayerId = [](World& aGame) {
        for (const entt::entity cEntity : aGame.view<LocalComponent, FormIdComponent>())
            if (aGame.get<FormIdComponent>(cEntity).Id == FakeEngine::kPlayer)
                return aGame.get<LocalComponent>(cEntity).Id;
        return uint32_t{};
    };
    const uint32_t cHost = cPlayerId(host);
    const uint32_t cOtherGuest = cPlayerId(party.Guest(1));
    REQUIRE(cHost);
    REQUIRE(cOtherGuest);
    party.Until([&] {
        const auto cPresent = [&](uint32_t aId) {
            return std::ranges::any_of(guest.view<RemoteComponent, PlayerComponent>(), [&](entt::entity aEntity) {
                return guest.get<RemoteComponent>(aEntity).Id == aId;
            });
        };
        return cPresent(cHost) && cPresent(cOtherGuest);
    }, "the host's and other guest's player copies are present");
    party.As(guest, [&] {
        NotifyAiState state;
        state.SessionId = cOtherGuest;
        state.State.Scene = GameId(0, 0x1234);
        state.State.Package = GameId(0, 0x5678);
        guest.GetDispatcher().trigger(state);
        CHECK_FALSE(Party::Engine(guest).AiDriven);

        state.SessionId = cHost;
        guest.GetDispatcher().trigger(state);
        REQUIRE(Party::Engine(guest).AiDriven);
        CHECK(Party::Engine(guest).ScenePackage == 0x5678);

        state.SessionId = cOtherGuest;
        state.State = {};
        guest.GetDispatcher().trigger(state);
        CHECK(Party::Engine(guest).AiDriven);

        state.SessionId = cHost;
        guest.GetDispatcher().trigger(state);
        CHECK_FALSE(Party::Engine(guest).AiDriven);
    });
}

SCENARIO("A world script on the guest takes everyone's controls; the guest leaves; the host gets them back",
         "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    WHEN("a world script on the guest takes the controls")
    {
        party.WorldScript(guest, DisablePlayerControls());
        party.Until([&] { return !Party::Engine(host).Has(kMovement); }, "the host's movement is taken");

        THEN("both games' movement and fighting are taken, held by the guest's game on the host")
        {
            CHECK(!Party::Engine(guest).Has(kMovement | kFighting));
            CHECK(!Party::Engine(host).Has(kFighting));
            CHECK(Party::Engine(host).Holds.HeldBy(kThisGame) == 0);
            CHECK(Party::Engine(host).Holds.Held() ==
                  0x541); // movement and jumping, fighting, menus: as Skyrim takes them
        }

        AND_WHEN("the guest leaves mid-cutscene")
        {
            party.Leave(guest);
            party.Until([&] { return Party::Engine(host).Has(kMovement | kFighting | kMainFour); },
                        "the host's controls come back");

            THEN("nothing is held on the host") CHECK(Party::Engine(host).Holds.Held() == 0);
        }
    }
}

SCENARIO("A world script on the host takes everyone's controls; the guest leaves and gets its own back", "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.WorldScript(host, DisablePlayerControls());
    party.Until([&] { return !Party::Engine(guest).Has(kMovement); }, "the guest's movement is taken");

    WHEN("the guest leaves")
    {
        party.Leave(guest);
        party.Until([&] { return Party::Engine(guest).Has(kMovement | kFighting); }, "the guest's controls come back");

        THEN("the host's cutscene still holds the host's") CHECK(!Party::Engine(host).Has(kMovement));
        AND_WHEN("the host's script gives them back")
        {
            party.WorldScript(host, EnablePlayerControls());
            THEN("the host's come back") CHECK(Party::Engine(host).Has(kMovement | kFighting));
        }
    }
}

SCENARIO("Two cutscenes at once: the first to end gives back only its own", "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.WorldScript(host, DisablePlayerControls());
    party.WorldScript(guest, DisablePlayerControls(true, false, false, false, true));
    party.Until([&] { return !Party::Engine(host).Has(kSneaking) && !Party::Engine(guest).Has(kFighting); },
                "each game's cutscene reaches the other");

    WHEN("the host's cutscene ends first")
    {
        party.WorldScript(host, EnablePlayerControls());
        party.Until([&] { return Party::Engine(guest).Has(kFighting); }, "the guest's fighting comes back");

        THEN("movement stays taken on both: the guest's cutscene holds it")
        {
            CHECK(!Party::Engine(host).Has(kMovement));
            CHECK(!Party::Engine(guest).Has(kMovement));
            CHECK(Party::Engine(host).Has(kFighting));
        }
    }
}

SCENARIO("A restriction stays with its game", "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    WHEN("a world script on the host takes fighting only")
    {
        party.WorldScript(host, DisablePlayerControls(false, true, false, false, false, false, false));
        // A marker after it: the guest has had every message the host sent before.
        party.WorldScript(host, DisablePlayerControls(true, false, false, false, false, false, false));
        party.Until([&] { return !Party::Engine(guest).Has(kMovement); }, "the marker reaches the guest");

        THEN("the guest can still fight")
        {
            CHECK(!Party::Engine(host).Has(kFighting));
            CHECK(Party::Engine(guest).Has(kFighting));
        }
    }
}

// Step 2 (cutscenes.md): what a story does to the player's body, each game does to its own player.
namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // where everyone starts
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior
constexpr uint32_t kMarker = 0x000A1B2C;   // an XMarker in the sinkhole (MQ206's Alias_TimeWound)
constexpr uint32_t kBack = 0x000A1B2D;     // one back in QASmoke (the vision over)
constexpr uint32_t kIdle = 0x0006A1F3;     // an idle (IdleForm)

void SceneWorld(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.References[kMarker] = {.Cell = kSinkhole, .Position = {100.f, 200.f, 0.f}};
    aEngine.References[kBack] = {.Cell = kSmoke, .Position = {-50.f, 30.f, 0.f}};
}

LocalCall MoveTo(uint32_t aWho, uint32_t aMarker = kMarker)
{
    return {.Class = "ObjectReference", .Function = "MoveTo", .Self = LocalForm{aWho}, .Arguments = {LocalForm{aMarker}}};
}

bool At(const FakeEngine& acEngine, uint32_t aCell, float aX)
{
    const auto& cPlayer = acEngine.Actors.at(FakeEngine::kPlayer);
    return cPlayer.Cell == aCell && cPlayer.Position[0] == aX;
}

bool AtMarker(const FakeEngine& acEngine)
{
    const auto& cPlayer = acEngine.Actors.at(FakeEngine::kPlayer);
    return cPlayer.Cell == kSinkhole && cPlayer.Position[0] == 100.f && cPlayer.Position[1] == 200.f;
}

// The host's copy of the guest's character.
uint32_t GuestCharacter(const FakeEngine& acHost)
{
    for (const auto& [cId, cActor] : acHost.Actors)
        if (cActor.Copy && cActor.Player)
            return cId;
    return 0;
}
} // namespace

SCENARIO("A story's MoveTo on the player moves each game's own player; the mirrored move leads no one", "[cutscene][body]")
{
    Party party(1, SceneWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return GuestCharacter(host) != 0; }, "the host shows the guest's character");
    const size_t cHostTravels = host.Travels;

    // The vision and back: Game.GetPlayer().MoveTo(Alias_TimeWound), then MoveTo the way back, in one stage.
    party.WorldScript(party.Host(), MoveTo(FakeEngine::kPlayer));
    party.WorldScript(party.Host(), MoveTo(FakeEngine::kPlayer, kBack));
    party.Until([&] { return At(host, kSmoke, -50.f) && At(guest, kSmoke, -50.f); }, "both players are back");
    for (int i = 0; i < 60; ++i)
        party.Frame();

    THEN("the host travelled twice, its own moves: the guest's mirrored moves took no one along")
    {
        CHECK(host.Travels == cHostTravels + 2);
        CHECK(At(host, kSmoke, -50.f));
    }
    party.Agree("the games agree on the players back in QASmoke");
}

SCENARIO("A story's idle on the player plays on each game's own player", "[cutscene][body]")
{
    Party party(1, SceneWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.WorldScript(party.Host(), {.Class = "Actor", .Function = "PlayIdle", .Self = LocalForm{FakeEngine::kPlayer},
                                     .Arguments = {LocalForm{kIdle}}});
    party.Until([&] { return !guest.Idles[FakeEngine::kPlayer].empty(); }, "the guest's player plays the idle");
    CHECK(host.Idles[FakeEngine::kPlayer] == std::vector<uint32_t>{kIdle});
    CHECK(guest.Idles[FakeEngine::kPlayer] == std::vector<uint32_t>{kIdle});
    THEN("no copy played it: each copy follows its owner")
    for (const auto& [cActor, cIdles] : host.Idles)
        CHECK(cActor == FakeEngine::kPlayer);
}
