// Death and respawn across games (docs/design/copy-contract.md): a copy dies by its owner's death, never by its own
// health; a player dies on its own game and is revived there, and the others spawn its copy again.
#include "Party.h"
#include "Fixtures.h"

#include <Events/SessionNoticeEvent.h>
#include <Messages/NotifyRespawn.h>
#include <Components.h>
#include <Events/PlayerDeathEvent.h>
#include <Services/DeathService.h>
#include <Services/SaveService.h>
#include <Services/JoinService.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerLooksChangeEvent.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr glm::vec3 kSmokeMarker{500.f, 400.f, 0.f}; // QASmoke's COC marker

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.CocMarkers[kSmoke] = kSmokeMarker;
    aEngine.Player.Level = 7;
    aEngine.References[0xECA].Cell = kSmoke;
    aEngine.References[0xECA].Locked = true;
}

// aGame's chat lines about the session (SessionNoticeEvent), "red: " before the ones it shows in red.
struct ChatNotices
{
    explicit ChatNotices(World& aGame)
        : Connection(aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&ChatNotices::On>(*this))
    {
    }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back((acNotice.Perished ? "red: " : "") + acNotice.Text); }
    [[nodiscard]] bool Has(const std::string& acLine) const { return std::ranges::find(Lines, acLine) != Lines.end(); }
    std::vector<std::string> Lines;
    entt::scoped_connection Connection;
};

// aGame's copy of the other game's player, by form id (0: none).


// aGame's dead player copies.
size_t DeadPlayerCopies(const FakeEngine& acEngine)
{
    return std::ranges::count_if(acEngine.CopiesOf(), [&](uint32_t aId) { return acEngine.Actors.at(aId).Player && acEngine.Actors.at(aId).Dead; });
}
} // namespace

SCENARIO("A host NPC dies: its copy dies with it", "[death]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {20.f, 20.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the host's guard");
    party.Agree("the games agree on the guard");

    WHEN("the host's player kills the guard")
    {
        const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
        party.As(party.Guest(), [&] { guest.Hit(cCopy, 200.f); });
        REQUIRE(guest.GetActorValue(cCopy, FakeEngine::kHealth) <= 0.f);
        CHECK_FALSE(guest.IsActorDead(cCopy));
        party.As(party.Host(), [&] { host.Hit(guard, 200.f); });
        REQUIRE(host.IsActorDead(guard));
        party.Until([&] { return guest.GetActorValue(cCopy, FakeEngine::kHealth) <= 0.f; },
                    "the copy shows the guard's health",
                    [&] {
                        return std::format("the guard's health {}, its copy's {}",
                                           host.GetActorValue(guard, FakeEngine::kHealth),
                                           guest.GetActorValue(cCopy, FakeEngine::kHealth));
                    });
        THEN("it is not dead by its own health (deferred kill), but by the guard's death")
        {
            party.Until([&] { return guest.IsActorDead(cCopy); }, "the guest's copy dies");
            party.Agree("the games agree on the dead guard");
        }
    }
}

SCENARIO("A player joining, going down and leaving: every game's chat says so, going down in red", "[death][session]")
{
    Party party(1, Plugins, false);
    FakeEngine& guest = Party::Engine(party.Guest());
    const ChatNotices cHostChat(party.Host());
    const ChatNotices cGuestChat(party.Guest());
    party.Join(party.Guest());
    party.Until([&] { return cHostChat.Has("Lydia joined the party."); }, "the host's chat says the guest joined");
    party.Agree("the games agree on the players");
    THEN("the guest's chat names nobody joining: the host was there already")
    {
        CHECK(std::ranges::none_of(cGuestChat.Lines, [](const auto& acLine) { return acLine.ends_with("joined the party."); }));
    }

    WHEN("the guest's player takes a deadly hit")
    {
        party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
        party.Until([&] { return cHostChat.Has("red: Lydia has perished.") && cGuestChat.Has("red: Lydia has perished."); },
                    "each game's chat says, in red, that the guest's player perished");
    }

    WHEN("the guest leaves")
    {
        party.Leave(party.Guest());
        party.Until([&] { return cHostChat.Has("Lydia left the party."); }, "the host's chat says the guest left");
    }
}

namespace
{
glm::vec3 At(const FakeEngine& acEngine)
{
    const auto& p = acEngine.Actors.at(FakeEngine::kPlayer);
    return {p.Position[0], p.Position[1], p.Position[2]};
}
void Frames(Party& aParty, size_t aCount)
{
    for (size_t i = 0; i < aCount; ++i)
        aParty.Frame();
}
}

SCENARIO("A guest stays down through combat, then respawns beside the standing host", "[death]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    party.As(party.Host(), [&] { host.PlayerStands(kSmoke, {700.f, 800.f, 0.f}); });
    party.As(party.Guest(), [&] {
        guest.PlayerStands(kSmoke, {-300.f, 100.f, 0.f});
        guest.ForceActorValue(FakeEngine::kPlayer, 0, FakeEngine::kMagicka, 5000.f);
    });
    party.Agree("movement and the owner's 5000 magicka reach its copy");
    const auto cHostAt = At(host);
    const auto cCopy = PlayerCopy(host);
    const auto cMade = host.Actors.at(cCopy).Made;
    const auto cFallAt = At(guest);
    const auto cTravels = host.Travels;
    uint32_t guard{};
    party.As(party.Host(), [&] {
        guard = host.LoadNpc(kGuard, kSmoke, {750.f, 800.f, 0.f});
        host.Fights(guard, FakeEngine::kPlayer);
    });
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.Until([&] { return death.IsDown(); }, "the guest reports being down");
    party.Until([&] { return death.Spectating() != 0; }, "the guest watches the standing host");
    Frames(party, 720);
    CHECK(death.IsDown());
    CHECK(At(guest) == cFallAt);

    WHEN("combat ends, then starts again before ten seconds")
    {
        party.As(party.Host(), [&] { host.Fights(guard, 0); });
        Frames(party, 300);
        CHECK(death.IsDown());
        party.As(party.Host(), [&] { host.Fights(guard, FakeEngine::kPlayer); });
        Frames(party, 360);
        CHECK(death.IsDown());
        party.As(party.Host(), [&] { host.Fights(guard, 0); });
        Frames(party, 540);
        CHECK(death.IsDown());
        Frames(party, 120);
        party.Until([&] { return !death.IsDown(); }, "the guest respawns after ten clear seconds");
        CHECK(At(guest) != cFallAt);
        CHECK(glm::distance(At(guest), cHostAt) <= 200.f);
        CHECK(At(guest) != cHostAt);
        CHECK(At(host) == cHostAt);
        CHECK(host.Travels == cTravels);
        CHECK(death.Spectating() == 0);
        party.Until([&] {
            const auto copy = PlayerCopy(host);
            return copy && (copy != cCopy || host.Actors.at(copy).Made != cMade);
        }, "the other game shows a fresh living player rather than standing the fallen copy up");
        party.Agree("copies show the respawned player with its current values");
        CHECK(guest.GetActorValue(FakeEngine::kPlayer, FakeEngine::kMagicka) == 5000.f);
        CHECK(host.GetActorValue(PlayerCopy(host), FakeEngine::kMagicka) == 5000.f);
    }
}

SCENARIO("Healing cannot revive a downed player early", "[death]")
{
    Party party(1, Plugins);
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.Until([&] { return death.IsDown(); }, "the guest is down");
    party.As(party.Guest(), [&] { guest.ForceActorValue(FakeEngine::kPlayer, 2, FakeEngine::kHealth, 100.f); });
    Frames(party, 300);
    CHECK(death.IsDown());
    CHECK(!guest.IsPlayerAlive());
}

SCENARIO("A player really dies: its copies die with it, it spectates once dead, and its game never reloads", "[death]")
{
    Party party(2, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    auto& other = Party::Engine(party.Guest(1));
    party.Agree("all players are present");
    uint32_t guard{};
    party.As(party.Host(), [&] {
        guard = host.LoadNpc(kGuard, kSmoke, {750.f, 800.f, 0.f});
        host.Fights(guard, FakeEngine::kPlayer);
    });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    party.Until([&] { return death.IsDown(); }, "the guest goes down as it dies");
    CHECK(!guest.IsPlayerAlive());
    CHECK(death.Spectating() == 0);
    party.Until([&] { return DeadPlayerCopies(host) == 1 && DeadPlayerCopies(other) == 1; },
                "the other games' copies of the guest die by its death action");
    party.Until([&] { return death.Spectating() != 0; }, "spectating starts once the guest is dead (its ragdoll at rest)");
    Frames(party, FakeEngine::kDeathReloadFrames);
    CHECK(!guest.Reloaded);

    const auto cFirst = death.Spectating();
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(SpectateCycleEvent{1}); });
    CHECK(death.Spectating() != cFirst);
    party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(SpectateCycleEvent{-1}); });
    CHECK(death.Spectating() == cFirst);

    WHEN("the party recovers")
    {
        party.As(party.Host(), [&] { host.Fights(guard, 0); });
        Frames(party, 660);
        party.Until([&] { return !death.IsDown(); }, "the guest respawns");
        CHECK(guest.IsPlayerAlive());
        CHECK(guest.GetActorValue(FakeEngine::kPlayer, FakeEngine::kHealth) > 0.f);
        party.Until([&] { return PlayerCopy(host) && PlayerCopy(other) && DeadPlayerCopies(host) == 0 && DeadPlayerCopies(other) == 0; },
                    "every game shows the guest alive again", [&] {
                        std::string out;
                        for (const FakeEngine* pGame : {&host, &other})
                            for (const uint32_t cId : pGame->CopiesOf())
                                if (pGame->Actors.at(cId).Player)
                                    out += std::format("{}:{:X} dead {} made {} base {:X}; ", pGame == &host ? "host" : "other", cId,
                                                       pGame->Actors.at(cId).Dead, pGame->Actors.at(cId).Made, pGame->Actors.at(cId).Base);
                        return out;
                    });
        CHECK(!guest.Reloaded);
    }

    WHEN("the guest leaves while down")
    {
        party.Leave(party.Guest());
        CHECK(!death.IsDown());
        CHECK(death.Spectating() == 0);
        THEN("its game's own death rules are back: the reload is not held")
        {
            CHECK(!guest.DeathReloadHeld);
        }
    }
}

SCENARIO("Spectating follows a standing player's replacement copy", "[death]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("both players are present");
    party.As(party.Host(), [&] { host.Fights(FakeEngine::kPlayer, PlayerCopy(host)); });
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.Until([&] { return death.Spectating() != 0; }, "the guest watches the standing host");
    const auto old = PlayerCopy(guest);
    party.As(party.Host(), [&] { party.Host().GetDispatcher().trigger(PlayerLooksChangeEvent{}); });
    party.Until([&] { return PlayerCopy(guest) && PlayerCopy(guest) != old; }, "the host's changed looks replace its copy");
    party.Until([&] { return guest.CameraTarget == PlayerCopy(guest); }, "the camera watches the replacement copy", [&] { return std::format("target {:X} copy {:X} old {:X} spect {}", guest.CameraTarget, PlayerCopy(guest), old, death.Spectating()); });
    CHECK(death.IsDown());
}

SCENARIO("A party wipe loads the previous host save and saved guest character", "[death][join]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("both players are present");
    // The guest stands away from the host at the checkpoint, where the wipe brings it back.
    auto& guestPlayer = guest.Actors.at(FakeEngine::kPlayer);
    guestPlayer.Position[0] += 500.f;
    const float cGuestAt = guestPlayer.Position[0];
    party.Until([&] { return std::abs(host.Actors.at(PlayerCopy(host)).Position[0] - cGuestAt) < 1.f; },
                "the host's copy of the guest walks with it");
    party.As(party.Host(), [&] { (void)host.SaveNow("BeforeBattle"); });
    party.Until([&] { return host.MadeSave.has_value(); }, "the host saves its checkpoint");
    party.As(party.Host(), [&] { (void)host.SaveNow("BeforeBattle"); });
    const size_t saves = host.Saves.size();
    party.As(party.Host(), [&] { host.SetLock(0xECA, false, 0); });
    party.As(party.Guest(), [&] {
        guest.Player.Level = 22;
        party.Guest().GetDispatcher().trigger(MenuEvent{"InventoryMenu", false});
    });
    party.Until([&] { return !guest.References.at(0xECA).Locked; }, "the world changes after the checkpoint");
    party.As(party.Host(), [&] { host.Hit(FakeEngine::kPlayer, 200.f); });
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    party.Until([&] { return !host.Loaded.empty() && !guest.Loaded.empty(); }, "both games load the previous checkpoint");
    party.Until([&] { return party.Host().GetTransport().IsOnline() && party.Guest().GetTransport().IsOnline(); }, "the party rejoins in the restored world");
    CHECK(!party.Host().ctx().at<DeathService>().IsDown());
    CHECK(!party.Guest().ctx().at<DeathService>().IsDown());
    CHECK(host.Saves.size() == saves);
    CHECK(host.Loaded.size() == 1);
    CHECK(guest.Loaded.size() == 1);
    CHECK(guest.Player.Name == "Lydia");
    CHECK(guest.Player.Level == 7);
    CHECK(std::abs(guestPlayer.Position[0] - cGuestAt) < 1.f);
    CHECK(std::abs(guestPlayer.Position[0] - host.Actors.at(FakeEngine::kPlayer).Position[0]) > 400.f);
    CHECK(host.References.at(0xECA).Locked);
    CHECK(guest.References.at(0xECA).Locked);
    party.Agree("the restored players have their own copies");
}

SCENARIO("A party wipe without a checkpoint reports failure and never revives in place", "[death]")
{
    Party party(1, Plugins);
    party.Agree("both players are present");
    party.As(party.Host(), [&] { Party::Engine(party.Host()).Hit(FakeEngine::kPlayer, 200.f); });
    party.As(party.Guest(), [&] { Party::Engine(party.Guest()).Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.Until([&] { return death.Status().starts_with("No previous gameplay save"); }, "the guest sees the checkpoint failure");
    Frames(party, 900);
    CHECK(death.IsDown());
    CHECK(Party::Engine(party.Host()).Loaded.empty());
    CHECK(Party::Engine(party.Guest()).Loaded.empty());
}

SCENARIO("A standing guest's combat holds a downed host", "[death]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("both players are present");
    party.As(party.Guest(), [&] { guest.Fights(FakeEngine::kPlayer, PlayerCopy(guest)); });
    party.As(party.Host(), [&] { host.Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Host().ctx().at<DeathService>();
    party.Until([&] { return death.Status() == "Party in combat"; }, "the host counts the standing guest's combat");
    Frames(party, 720);
    CHECK(death.IsDown());
    party.As(party.Guest(), [&] { guest.Fights(FakeEngine::kPlayer, 0); });
    Frames(party, 660);
    party.Until([&] { return !death.IsDown(); }, "the host respawns beside the guest");
    CHECK(glm::distance(At(host), At(guest)) <= 200.f);
}

SCENARIO("Respawning a downed player does not give back a cutscene's controls", "[death][cutscene]")
{
    Party party(1, Plugins);
    auto& guest = Party::Engine(party.Guest());
    party.Agree("both players are present");
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.Until([&] { return death.Spectating() != 0; }, "the guest watches the host");
    party.WorldScript(party.Host(), DisablePlayerControls());
    party.Until([&] { return guest.CameraTarget == FakeEngine::kPlayer; }, "the shared cutscene owns the guest's camera");
    Frames(party, 660);
    party.Until([&] { return !death.IsDown(); }, "the guest respawns during the cutscene");
    CHECK(!guest.Has(PartyRules::kMovement));
    party.WorldScript(party.Host(), EnablePlayerControls());
    party.Until([&] { return guest.Has(PartyRules::kMovement); }, "the cutscene gives movement back");
}

SCENARIO("Respawn uses the standing party's current location after travel", "[death][travel]")
{
    constexpr uint32_t inn = 0x13419;
    Party party(1, [](FakeEngine& e) { Plugins(e); e.LoadCell(inn); });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("both players are present");
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    auto& death = party.Guest().ctx().at<DeathService>();
    party.Until([&] { return death.IsDown(); }, "the guest is down");
    party.As(party.Host(), [&] { host.MoveActorTo(FakeEngine::kPlayer, inn, 800.f, 900.f, 0.f); });
    party.Until([&] { return guest.GetPlayerParentCellId() == inn; }, "the downed player follows the party's travel");
    Frames(party, 660);
    party.Until([&] { return !death.IsDown(); }, "the guest respawns at the party's new location");
    CHECK(guest.GetPlayerParentCellId() == inn);
    CHECK(glm::distance(At(guest), At(host)) <= 200.f);
    CHECK(At(guest) != kSmokeMarker);
}

SCENARIO("Rebuilding a player's copy retains its owner's latest magicka", "[death][values]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the player was announced with 100 magicka");
    party.As(party.Guest(), [&] { guest.ForceActorValue(FakeEngine::kPlayer, 0, FakeEngine::kMagicka, 5000.f); });
    party.Until([&] { return host.GetActorValue(PlayerCopy(host), FakeEngine::kMagicka) == 5000.f; },
                "the copy receives the owner's 5000 magicka");
    uint32_t session = 0;
    for (const auto e : party.Host().view<RemoteComponent, PlayerComponent>())
        session = party.Host().get<RemoteComponent>(e).Id;
    REQUIRE(session);
    const auto old = PlayerCopy(host);
    const auto made = host.Actors.at(old).Made;
    party.As(party.Host(), [&] {
        NotifyRespawn rebuild;
        rebuild.ActorId = session;
        party.Host().GetDispatcher().trigger(rebuild);
    });
    party.Until([&] { return PlayerCopy(host) && (PlayerCopy(host) != old || host.Actors.at(PlayerCopy(host)).Made != made); }, "the host builds a fresh copy");
    party.Agree("a fresh copy still has the owner's accepted values");
    CHECK(host.GetActorValue(PlayerCopy(host), FakeEngine::kMagicka) == 5000.f);
}

SCENARIO("Spectating is third person only: a view dropped into the watched player's head goes back out", "[death]")
{
    Party party(1, Plugins);
    auto& guest = Party::Engine(party.Guest());
    party.Agree("both players are present");
    auto& death = party.Guest().ctx().at<DeathService>();
    party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
    party.Until([&] { return death.Spectating() != 0; }, "the dead guest spectates the host");

    guest.FirstPerson = true; // the POV key, or a zoom all the way in
    party.Until([&] { return !guest.FirstPerson; }, "the view goes back to third person");

    WHEN("the guest stands again")
    {
        party.As(party.Host(), [] {});
        Frames(party, 660);
        party.Until([&] { return !death.IsDown(); }, "the guest respawns");
        guest.FirstPerson = true;
        Frames(party, 5);
        THEN("its own first person view is its to keep") CHECK(guest.FirstPerson);
    }
}
