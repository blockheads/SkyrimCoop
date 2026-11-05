// Friendly fire (docs/design/friendly-fire.md): a player's hit or spell on another player's copy goes to that
// player's game, which applies it to its real player; a teammate's kill is a real death, named in every chat.
#include "Party.h"
#include "Fixtures.h"
#include <Games/Animation/ReplayState.h>

#include <Events/SessionNoticeEvent.h>
#include <Events/MeleeStaggerEvent.h>
#include <Services/DeathService.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kFirebolt = 0x0001C789;
constexpr uint32_t kFireboltEffect = 0x00012EF7;

// aIn's copy of aOf's player (0: none yet).
uint32_t CopyOfPlayer(Party& aParty, World& aIn, World& aOf)
{
    uint32_t session = 0, copy = 0;
    aParty.As(aOf, [&] { session = Utils::GetSessionIdByFormId(FakeEngine::kPlayer).value_or(0); });
    if (session)
        aParty.As(aIn, [&] { copy = Utils::GetFormIdBySessionId(session); });
    return copy;
}

// aGame's chat lines about the session.
struct ChatNotices
{
    explicit ChatNotices(World& aGame)
        : Connection(aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&ChatNotices::On>(*this))
    {
    }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back(acNotice.Text); }
    [[nodiscard]] bool Has(const std::string& acLine) const { return std::ranges::find(Lines, acLine) != Lines.end(); }
    std::vector<std::string> Lines;
    entt::scoped_connection Connection;
};

float Health(FakeEngine& aEngine)
{
    return aEngine.GetActorValue(FakeEngine::kPlayer, FakeEngine::kHealth);
}

// aAttacker's player strikes aVictim's player's copy on aAttacker's game until aVictim's own player has taken it.
void Strike(Party& aParty, World& aAttacker, World& aVictim, float aDamage)
{
    FakeEngine& attacker = Party::Engine(aAttacker);
    FakeEngine& victim = Party::Engine(aVictim);
    aParty.Until([&] { return CopyOfPlayer(aParty, aAttacker, aVictim) != 0; },
                 std::format("{}'s game has {}'s player", attacker.Name, victim.Name));
    const float cBefore = Health(victim);
    const uint32_t cCopy = CopyOfPlayer(aParty, aAttacker, aVictim);
    aParty.As(aAttacker, [&] { attacker.Strikes(FakeEngine::kPlayer, cCopy, aDamage); });
    aParty.Until([&] { return Health(victim) == cBefore - aDamage; },
                 std::format("{}'s own player takes {}'s blow", victim.Name, attacker.Name));
}
} // namespace

SCENARIO("A guest's sword on the host's player: the host's game applies it, and nobody turns on the guest",
         "[combat][friendly-fire]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    const float cFull = Health(host);

    Strike(party, party.Guest(), party.Host(), 30.f);
    THEN("the guest's copy of the host took nothing itself, and the host's player ran no NPC's reaction")
    {
        CHECK(guest.GetActorValue(CopyOfPlayer(party, party.Guest(), party.Host()), FakeEngine::kHealth) <= cFull);
        CHECK(std::ranges::none_of(host.Reactions, [](const auto& acReaction) { return acReaction.first == FakeEngine::kPlayer; }));
    }
    party.Until([&] { return guest.GetActorValue(CopyOfPlayer(party, party.Guest(), party.Host()), FakeEngine::kHealth) == cFull - 30.f; },
                "the guest's copy of the host shows the host's health");
}

SCENARIO("The host's sword on a guest's player: the guest's game applies it", "[combat][friendly-fire]")
{
    Party party(1, SmokeWorld);
    Strike(party, party.Host(), party.Guest(), 25.f);
}

SCENARIO("A guest's sword on another guest's player: the host passes it to that guest's game", "[combat][friendly-fire]")
{
    Party party(2, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    const float cHostFull = Health(host);
    Strike(party, party.Guest(), party.Guest(1), 20.f);
    CHECK(Health(host) == cHostFull);
}

SCENARIO("A guest's fire bolt on the host's player burns the host's own player", "[magic][friendly-fire]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Guest(), party.Host()) != 0; }, "the guest's game has the host's player");
    const float cFull = Health(host);

    const uint32_t cCopy = CopyOfPlayer(party, party.Guest(), party.Host());
    party.As(party.Guest(), [&] { guest.SpellHits(cCopy, FakeEngine::kPlayer, kFirebolt, kFireboltEffect, 40.f); });
    party.Until([&] { return Health(host) == cFull - 40.f; }, "the host's player takes the bolt");
    THEN("the effect landed on the host's player, from the guest's copy, with no NPC's reaction")
    {
        CHECK(std::ranges::any_of(host.Effects, [&](const auto& acEffect) {
            return acEffect.Target == FakeEngine::kPlayer && acEffect.Spell == kFirebolt &&
                   acEffect.Caster == CopyOfPlayer(party, party.Host(), party.Guest());
        }));
        CHECK(host.Reactions.empty());
    }
}

SCENARIO("A guest killed by another guest: a real death, spectating, and every chat names the killer",
         "[death][friendly-fire]")
{
    Party party(2, SmokeWorld);
    FakeEngine& victim = Party::Engine(party.Guest(1));
    const ChatNotices cHostChat(party.Host());
    const ChatNotices cKillerChat(party.Guest());
    party.Agree("all players are present");

    Strike(party, party.Guest(), party.Guest(1), Health(victim));
    auto& death = party.Guest(1).ctx().at<DeathService>();
    party.Until([&] { return death.IsDown(); }, "the victim goes down");
    CHECK(!victim.IsPlayerAlive());
    CHECK(victim.Actors.at(FakeEngine::kPlayer).KilledBy == CopyOfPlayer(party, party.Guest(1), party.Guest()));
    party.Until([&] { return cHostChat.Has("Aela was killed by Lydia.") && cKillerChat.Has("Aela was killed by Lydia."); },
                "every chat says who killed the victim");
    party.Until([&] { return death.Spectating() != 0; }, "the victim spectates once dead", [&] {
        std::string out = std::format("down {} dead {} dying {}; ", death.IsDown(), victim.Actors.at(FakeEngine::kPlayer).Dead,
                                      victim.Actors.at(FakeEngine::kPlayer).DyingFrames);
        for (const uint32_t cId : victim.CopiesOf())
            if (victim.Actors.at(cId).Player)
                out += std::format("copy {:X} dead {} 3d {}; ", cId, victim.Actors.at(cId).Dead, victim.ActorHas3D(cId));
        return out;
    });
}

// aAttacker's player starts a kill move on aVictim's player's copy, as PerformAction does on its game.
void StartKillMove(Party& aParty, World& aAttacker, World& aVictim, const char* acpEvent)
{
    FakeEngine& attacker = Party::Engine(aAttacker);
    ActionEvent action;
    action.ActionId = 0x13005;
    action.EventName = std::string("pa_") + acpEvent;
    action.TargetEventName = acpEvent;
    action.TargetId = CopyOfPlayer(aParty, aAttacker, aVictim);
    aParty.As(aAttacker, [&] { REQUIRE(attacker.PerformAnimation(FakeEngine::kPlayer, action)); });
}

// aOwner's player performs a plain action; its copy on aOther takes it after every action before it.
void CopyCatchesUp(Party& aParty, World& aOwner, World& aOther, uint32_t aIdle)
{
    FakeEngine& owner = Party::Engine(aOwner);
    FakeEngine& other = Party::Engine(aOther);
    ActionEvent action;
    action.ActionId = 0x959F9;
    action.IdleId = aIdle;
    action.EventName = "moveStop";
    aParty.As(aOwner, [&] { REQUIRE(owner.PerformAnimation(FakeEngine::kPlayer, action)); });
    const uint32_t cCopy = CopyOfPlayer(aParty, aOther, aOwner);
    aParty.Until([&] { return other.GetActorLatestAnimation(cCopy).IdleId == aIdle; }, "the copy caught up with its owner's actions");
}

// Both players act at once; each copy takes it after every action before it (aFrames: how long that took).
void BothCatchUp(Party& aParty, uint32_t aHostIdle, uint32_t aGuestIdle, int& aFrames)
{
    FakeEngine& host = Party::Engine(aParty.Host());
    FakeEngine& guest = Party::Engine(aParty.Guest());
    ActionEvent action;
    action.ActionId = 0x959F9;
    action.EventName = "moveStop";
    action.IdleId = aHostIdle;
    aParty.As(aParty.Host(), [&] { REQUIRE(host.PerformAnimation(FakeEngine::kPlayer, action)); });
    action.IdleId = aGuestIdle;
    aParty.As(aParty.Guest(), [&] { REQUIRE(guest.PerformAnimation(FakeEngine::kPlayer, action)); });
    const uint32_t cOnGuest = CopyOfPlayer(aParty, aParty.Guest(), aParty.Host());
    const uint32_t cOnHost = CopyOfPlayer(aParty, aParty.Host(), aParty.Guest());
    aParty.Until([&] { ++aFrames; return guest.GetActorLatestAnimation(cOnGuest).IdleId == aHostIdle &&
                                        host.GetActorLatestAnimation(cOnHost).IdleId == aGuestIdle; },
                 "both copies caught up with their owners' actions");
}

SCENARIO("A kill move plays both halves on the victim's game", "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Host(), party.Guest()) && CopyOfPlayer(party, party.Guest(), party.Host()); },
                "both games have the other's player");

    StartKillMove(party, party.Host(), party.Guest(), "KillMoveI");
    CopyCatchesUp(party, party.Host(), party.Guest(), 0x1000);
    CHECK(guest.Actors.at(FakeEngine::kPlayer).InKillMove);
    CHECK(guest.Actors.at(CopyOfPlayer(party, party.Guest(), party.Host())).InKillMove);

    // The victim's half comes back to the attacker's game: the same pair, which goes on.
    ActionEvent half;
    half.ActionId = 0x13005;
    half.EventName = "KillMoveI";
    half.TargetEventName = "pa_KillMoveI";
    half.TargetId = CopyOfPlayer(party, party.Guest(), party.Host());
    party.As(party.Guest(), [&] { guest.Actors.at(FakeEngine::kPlayer).InKillMove = false; REQUIRE(guest.PerformAnimation(FakeEngine::kPlayer, half)); });
    CopyCatchesUp(party, party.Guest(), party.Host(), 0x1001);
    CHECK(host.Actors.at(FakeEngine::kPlayer).InKillMove);
    CHECK(host.CollapsedPairs + guest.CollapsedPairs == 0);
}

SCENARIO("A victim's last move, sent before it heard of the kill move, leaves the pair alone", "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Host(), party.Guest()) && CopyOfPlayer(party, party.Guest(), party.Host()); },
                "both games have the other's player");

    StartKillMove(party, party.Host(), party.Guest(), "KillMove1HMBackStab");
    REQUIRE(host.Actors.at(CopyOfPlayer(party, party.Host(), party.Guest())).InKillMove);
    CopyCatchesUp(party, party.Guest(), party.Host(), 0x1000);
    CHECK(host.BrokenPairs == 0);
    CHECK(host.Actors.at(CopyOfPlayer(party, party.Host(), party.Guest())).InKillMove);
}

SCENARIO("A kill move's victim brushing a wall lives on to the clip's own kill", "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Host(), party.Guest()) && CopyOfPlayer(party, party.Guest(), party.Host()); },
                "both games have the other's player");

    StartKillMove(party, party.Host(), party.Guest(), "KillMove1HMBackStab");
    CopyCatchesUp(party, party.Host(), party.Guest(), 0x1000);
    REQUIRE(guest.Actors.at(FakeEngine::kPlayer).InKillMove);
    party.As(party.Guest(), [&] { guest.RagdollContact(FakeEngine::kPlayer); });
    CHECK(guest.IsPlayerAlive());
}

SCENARIO("A kill move's copy holds its pose from its clip's KillActor to its owner's death", "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Host(), party.Guest()) && CopyOfPlayer(party, party.Guest(), party.Host()); },
                "both games have the other's player");
    StartKillMove(party, party.Host(), party.Guest(), "KillMove1HMGrappleStab");
    CopyCatchesUp(party, party.Host(), party.Guest(), 0x1000);
    const uint32_t cCopy = CopyOfPlayer(party, party.Host(), party.Guest());
    party.As(party.Host(), [&] {
        host.ClipKillActor(cCopy);
        CHECK(host.PoseHeld(cCopy));
    });
    party.As(party.Guest(), [&] { guest.KillActor(FakeEngine::kPlayer); });
    party.Until([&] { return host.Actors.at(cCopy).Dead; }, "the copy dies with its owner");
    party.As(party.Host(), [&] { CHECK_FALSE(host.PoseHeld(cCopy)); });
}

SCENARIO("A kill move plays on a victim's game that has no room at the mark, and an attacker's own check still counts",
         "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Host(), party.Guest()) && CopyOfPlayer(party, party.Guest(), party.Host()); },
                "both games have the other's player");

    guest.PairMarkBlocked = true;
    StartKillMove(party, party.Host(), party.Guest(), "KillMove1HMBackStab");
    CopyCatchesUp(party, party.Host(), party.Guest(), 0x1000);
    CHECK(guest.Actors.at(FakeEngine::kPlayer).InKillMove);

    host.Actors.at(FakeEngine::kPlayer).InKillMove = guest.Actors.at(FakeEngine::kPlayer).InKillMove = false;
    ActionEvent own;
    own.ActionId = 0x13005;
    own.EventName = "pa_KillMoveI";
    own.TargetEventName = "KillMoveI";
    own.TargetId = CopyOfPlayer(party, party.Guest(), party.Host());
    party.As(party.Guest(), [&] { CHECK_FALSE(guest.PerformAnimation(FakeEngine::kPlayer, own)); });
}

namespace
{
// Crossed kill moves, aHostFirst's player a few frames ahead (same tick when aGap is 0); blows land on both games.
void CrossKillMoves(Party& aParty, bool aHostFirst, int aGap, uint32_t aStopsDropped)
{
    FakeEngine& host = Party::Engine(aParty.Host());
    FakeEngine& guest = Party::Engine(aParty.Guest());
    aParty.Until([&] { return CopyOfPlayer(aParty, aParty.Host(), aParty.Guest()) && CopyOfPlayer(aParty, aParty.Guest(), aParty.Host()); },
                 "both games have the other's player");
    host.PairBlows = guest.PairBlows = true;
    host.PairStopsDropped = guest.PairStopsDropped = aStopsDropped;
    World& first = aHostFirst ? aParty.Host() : aParty.Guest();
    World& second = aHostFirst ? aParty.Guest() : aParty.Host();
    StartKillMove(aParty, first, second, "KillMoveI");
    int frames = 0;
    if (aGap)
        aParty.Until([&] { return ++frames > aGap; }, "the second player swings a moment later");
    StartKillMove(aParty, second, first, "KillMoveD");
    int caught = 0;
    BothCatchUp(aParty, 0x1000, 0x1001, caught);
}
} // namespace

SCENARIO("Two players' kill moves cross: the first swing wins, on both games", "[combat][friendly-fire][kill-move]")
{
    const bool cHostFirst = GENERATE(true, false);
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    CrossKillMoves(party, cHostFirst, 5, 0);
    FakeEngine& winner = cHostFirst ? host : guest;
    FakeEngine& loser = cHostFirst ? guest : host;
    CHECK(host.CollapsedPairs + guest.CollapsedPairs == 0);
    CHECK(winner.Actors.at(FakeEngine::kPlayer).InPairClip);
    CHECK(loser.Actors.at(FakeEngine::kPlayer).InKillMove); // the winner's pair: it plays the victim's half
    CHECK(loser.Actors.at(FakeEngine::kPlayer).PairBlowIn == 0);

    party.Until([&] { return !host.IsPlayerAlive() || !guest.IsPlayerAlive(); }, "a blow lands");
    CHECK(winner.IsPlayerAlive());
    CHECK_FALSE(loser.IsPlayerAlive());
}

SCENARIO("Two players' kill moves on the same tick: both games pick the same winner", "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    CrossKillMoves(party, true, 0, 0);
    party.Until([&] { return !host.IsPlayerAlive() || !guest.IsPlayerAlive(); }, "a blow lands");
    CHECK(host.IsPlayerAlive() != guest.IsPlayerAlive());
}

SCENARIO("A yielded clip that drops pairedStop never lands its blow", "[combat][friendly-fire][kill-move]")
{
    const uint32_t cDropped = GENERATE(5u, 1000u); // cleared by the resends; or a clip that outlasts them
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    guest.PairBlowFrames = 1000; // the later swing's clip is shorter: its blow would land first
    CrossKillMoves(party, true, 5, cDropped);
    if (cDropped < ReplayState::cClashStopUpdates)
        CHECK(guest.Actors.at(FakeEngine::kPlayer).InKillMove); // its clip left, so the winner's pair could play
    party.Until([&] { return !host.IsPlayerAlive() || !guest.IsPlayerAlive(); }, "a blow lands");
    CHECK(host.IsPlayerAlive());
    CHECK_FALSE(guest.IsPlayerAlive());
}

SCENARIO("After yielding, a player's fresh swing lands", "[combat][friendly-fire][kill-move]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    host.PairBlowFrames = guest.PairBlowFrames = 1000000; // the winner's blow is not this flow's
    CrossKillMoves(party, true, 5, 0);
    ActionEvent swing;
    swing.ActionId = 0x13005;
    swing.EventName = "attackStart";
    party.As(party.Guest(), [&] { REQUIRE(guest.PerformAnimation(FakeEngine::kPlayer, swing)); });
    Strike(party, party.Guest(), party.Host(), 10.f);
}

SCENARIO("A player's melee stagger reaches the victim's owner once", "[friendly-fire][melee-stagger]")
{
    Party party(2, SmokeWorld);
    party.Agree("all players and copies are present");
    auto& attacker = Party::Engine(party.Guest());
    const bool cOtherGuest = GENERATE(false, true);
    World& victimGame = cOtherGuest ? party.Guest(1) : party.Host();
    auto& victim = Party::Engine(victimGame);
    uint32_t targetSession = 0, targetCopy = 0;
    party.As(victimGame, [&] { targetSession = *Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    party.As(party.Guest(), [&] { targetCopy = Utils::GetFormIdBySessionId(targetSession); });
    REQUIRE(targetCopy);
    party.As(party.Guest(), [&] {
        party.Guest().GetDispatcher().trigger(MeleeStaggerEvent{targetCopy, FakeEngine::kPlayer, 0.75f});
    });
    party.Until([&] { return victim.Staggers.size() == 1; }, "the victim's own game staggers it");
    CHECK(std::get<0>(victim.Staggers[0]) == FakeEngine::kPlayer);
    CHECK(std::get<2>(victim.Staggers[0]) == Approx(0.75f));
    CHECK(attacker.Staggers.empty());
    const auto cHealth = victim.GetActorValue(FakeEngine::kPlayer, 24);
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(victim.Staggers.size() == 1);
    CHECK(victim.GetActorValue(FakeEngine::kPlayer, 24) == cHealth);
}
