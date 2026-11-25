// HostClaims: action claims, one claim per actor (one-simulator.md, "Action claims").
#include "session.h"

namespace
{
// Whose game moves aActor: its owner's movement goes to the others.
bool MovedBy(Session& aSession, uint32_t aActor, ConnectionId_t aMover, ConnectionId_t aWatcher)
{
    aSession.Outbox.clear();
    ReferencesMoveRequest moved;
    moved.Updates[aActor] = {};
    aSession.Receive(aMover, moved);
    return !aSession.SentTo<NotifyReferencesMove>(aWatcher).empty();
}

ActorGiveBackRequest Back(uint32_t aActor, uint32_t aClaimId)
{
    ActorGiveBackRequest back;
    back.ActorId = aActor;
    back.ClaimId = aClaimId;
    return back;
}
} // namespace

TEST_CASE("Claims: all or none: one member held refuses the claim, and a removed member ends its claim", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);

    // The other player is busy in its own kill move on the wolf: a kill move naming it and anyone else is refused
    // whole, the free member untouched.
    session.Receive(kOtherPeer, KillMoveClaim(cOtherPlayer, cWolf));
    session.Outbox.clear();
    session.Receive(kPeer, KillMoveClaim(cPeerPlayer, cOtherPlayer, true));
    REQUIRE(session.SentTo<NotifyClaimRefused>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyClaimRefused>(kPeer)[0]->Reason == Payloads::Claim::kHeld);

    // A player in a claim leaves: the claim ends, its other members back (the wolf to the host).
    session.Outbox.clear();
    session.Leave(kOtherPeer);
    const auto cBack = session.SentTo<NotifyActorGiveBack>(kPeer);
    REQUIRE_FALSE(cBack.empty());
    CHECK(std::ranges::any_of(cBack, [&](const auto* apBack) { return apBack->ActorId == cWolf && apBack->Taken; }));
    CHECK(std::ranges::any_of(cBack, [](const auto* apBack) { return apBack->Ended; }));
}

TEST_CASE("Claims: a give-back after the claim passed to a passenger is not the holder's word", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Receive(kPeer, RideClaim(cPeerPlayer, cHorse));
    BoardRequest board;
    board.RiderId = cOtherPlayer;
    board.MountId = cHorse;
    session.Receive(kOtherPeer, board);
    session.Outbox.clear();

    // The driver gets off: its passenger's game holds the horse, every game told.
    DismountRequest off;
    off.RiderId = cPeerPlayer;
    off.MountId = cHorse;
    session.Receive(kPeer, off);
    const auto cClaims = session.SentTo<NotifyClaim>(kHost);
    REQUIRE(cClaims.size() == 1);
    CHECK(cClaims[0]->ForId == cOtherPlayer);

    // Its game's give-back of the first claim, after its last update, comes too late to count.
    session.Outbox.clear();
    session.Receive(kPeer, Back(cHorse, 1));
    CHECK(session.SentTo<NotifyActorGiveBack>(kHost).empty());
    CHECK(MovedBy(session, cHorse, kOtherPeer, kHost));
}

TEST_CASE("Claims: an older claim's give-back, or one naming none, is not this claim's; a ride's kill does not count", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, RideClaim(cPeerPlayer, cHorse));
    session.Receive(kPeer, Back(cHorse, 1));
    session.Taken(cHorse);
    session.Outbox.clear();
    session.Receive(kPeer, RideClaim(cPeerPlayer, cHorse));
    const auto cRide = session.SentTo<NotifyClaim>(kHost);
    REQUIRE(cRide.size() == 1);
    CHECK(cRide[0]->ClaimId == 2);

    session.Outbox.clear();
    session.Receive(kPeer, Back(cHorse, 1));
    session.Receive(kPeer, Back(cHorse, 0));
    CHECK(session.SentTo<NotifyActorGiveBack>(kHost).empty());
    auto back = Back(cHorse, 2);
    back.Killed = true;
    session.Receive(kPeer, back);
    REQUIRE(session.SentTo<NotifyActorGiveBack>(kHost).size() == 1);
    CHECK_FALSE(session.SentTo<NotifyActorGiveBack>(kHost)[0]->Killed);
}

TEST_CASE("Claims: a horse given back with its rider still marked ends the ride", "[HostAuthority][claims]")
{
    // Its rider's game could not take it (a player's summon): it gives it back at once, with no dismount.
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Receive(kPeer, RideClaim(cPeerPlayer, cHorse));
    session.Outbox.clear();
    session.Receive(kPeer, Back(cHorse, 1));
    REQUIRE(session.SentTo<NotifyDismount>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyDismount>(kOtherPeer)[0]->RiderId == cPeerPlayer);
    session.Taken(cHorse);
    session.Outbox.clear();
    session.Receive(kOtherPeer, RideClaim(cOtherPlayer, cHorse));
    CHECK(session.SentTo<NotifyClaimRefused>(kOtherPeer).empty());
}

TEST_CASE("Claims: a player joining while an actor is on its way back gets the host's copy of it", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, KillMoveClaim(cPeerPlayer, cWolf));
    session.Receive(kPeer, Back(cWolf, 1));

    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    CHECK(std::ranges::any_of(session.SentTo<NotifyCharacterSpawn>(cLate), [&](const auto* apSpawn) { return apSpawn->SessionId == cWolf; }));
}

TEST_CASE("Claims: a removal taking a claim's last outstanding member still ends the claim", "[HostAuthority][claims]")
{
    // The attacker's lock back first; the NPC victim, waiting for its next update, unloads on the claimer's game.
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, KillMoveClaim(cPeerPlayer, cWolf));
    session.Receive(kPeer, Back(cPeerPlayer, 1));
    session.Outbox.clear();
    RequestRemoveCharacter removal;
    removal.SessionId = cWolf;
    session.Receive(kPeer, removal);
    const auto cBack = session.SentTo<NotifyActorGiveBack>(kHost);
    REQUIRE(cBack.size() == 1);
    CHECK(cBack[0]->ClaimId == 1);
    CHECK(cBack[0]->Ended);
}

TEST_CASE("Claims: a player joining while a kill move is claimed hears the claim", "[HostAuthority][claims]")
{
    // Its replay of the attacker's kill move goes by the answer (ActionClaims): a taken victim has no stream half.
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, KillMoveClaim(cPeerPlayer, cWolf));

    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    const auto cClaims = session.SentTo<NotifyClaim>(cLate);
    REQUIRE(cClaims.size() == 1);
    CHECK(cClaims[0]->ForId == cPeerPlayer);
}

TEST_CASE("Claims: a player joining while a guest rides a horse gets the rider's word on its effects", "[HostAuthority][claims][effects]")
{
    // Review of #129: a guest's horse's spawn came from the host's copy, whose own list never holds the owner's effects.
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, RideClaim(cPeerPlayer, cHorse));
    RequestActiveEffects effects;
    effects.SessionId = cHorse;
    effects.EffectIds = {GameId(0, 0x12F03)};
    effects.Held = {{GameId(0, 0xA0370), GameId(0, 0x9F28F)}};
    session.Receive(kPeer, effects);

    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(cLate);
    const auto cSpawn = std::ranges::find_if(cSpawns, [&](const auto* apSpawn) { return apSpawn->SessionId == cHorse; });
    REQUIRE(cSpawn != cSpawns.end());
    CHECK((*cSpawn)->HeldEffects == effects.Held);
    CHECK((*cSpawn)->EffectIds == effects.EffectIds);
}
