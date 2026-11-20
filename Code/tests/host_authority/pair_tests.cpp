// Kill moves as action claims (HostClaims; one-simulator.md, "Action claims").
#include "session.h"

namespace
{
ClaimRequest Claim(uint32_t aActor, uint32_t aVictim, bool aVictimIsPlayer = false)
{
    return KillMoveClaim(aActor, aVictim, aVictimIsPlayer);
}

// Whose game a hit on aActor is sent to: its owner's.
ConnectionId_t OwnerSeen(Session& aSession, uint32_t aActor, ConnectionId_t aHitter)
{
    aSession.Outbox.clear();
    RequestHealthChangeBroadcast hit;
    hit.Id = aActor;
    hit.DeltaHealth = -1.f;
    aSession.Receive(aHitter, hit);
    for (const ConnectionId_t cPlayer : {kHost, kPeer, kOtherPeer})
        if (!aSession.SentTo<NotifyHealthChangeBroadcast>(cPlayer).empty())
            return cPlayer;
    return 0;
}
} // namespace

TEST_CASE("Kill moves: the first claim on a host NPC holds, the NPC its claimer's game's until the pair is done", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Outbox.clear();

    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    for (const ConnectionId_t cPlayer : {kHost, kPeer, kOtherPeer})
    {
        // One claim on both: the attacker locked, the wolf taken with its death kept for the give-back.
        const auto cClaims = session.SentTo<NotifyClaim>(cPlayer);
        REQUIRE(cClaims.size() == 1);
        CHECK(cClaims[0]->ForId == cPeerPlayer);
        CHECK(cClaims[0]->ClaimId == 1);
        REQUIRE(cClaims[0]->Members.size() == 2);
        CHECK(cClaims[0]->Members[1].ActorId == cWolf);
        CHECK(cClaims[0]->Members[1].DeathOnGiveBack);
    }
    CHECK(OwnerSeen(session, cWolf, kOtherPeer) == kPeer);
    CHECK(OwnerSeen(session, cPeerPlayer, kOtherPeer) == kPeer); // locked, its owner's still

    // Another player's kill move on the same wolf within the round trip: refused as held, told to every game.
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyClaim>(kHost).empty());
    const auto cRefused = session.SentTo<NotifyClaimRefused>(kOtherPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->Reason == Payloads::Claim::kHeld);
    CHECK(session.SentTo<NotifyClaimRefused>(kHost).size() == 1);

    // Its members given back, the wolf killed: the host's again, its killer the claim's actor; the claim ends with
    // its last member.
    session.Outbox.clear();
    ActorGiveBackRequest attacker;
    attacker.ActorId = cPeerPlayer;
    attacker.ClaimId = 1;
    ActorGiveBackRequest giveBack;
    giveBack.ActorId = cWolf;
    giveBack.ClaimId = 1;
    giveBack.Tick = 4200;
    giveBack.Killed = true;
    session.Receive(kOtherPeer, giveBack); // not its claim
    CHECK(session.SentTo<NotifyActorGiveBack>(kHost).empty());
    session.Receive(kPeer, attacker);
    session.Receive(kPeer, giveBack);
    const auto cBack = session.SentTo<NotifyActorGiveBack>(kHost);
    REQUIRE(cBack.size() == 2);
    CHECK_FALSE(cBack[0]->Taken);
    CHECK_FALSE(cBack[0]->Ended);
    CHECK(cBack[1]->Tick == 4200);
    CHECK(cBack[1]->Killed);
    CHECK(cBack[1]->Taken);
    CHECK(cBack[1]->Ended);
    CHECK(cBack[1]->ForId == cPeerPlayer);
    CHECK(OwnerSeen(session, cWolf, kOtherPeer) == kHost);

    // On its way back, claimed by nobody: a kill reported dead settles first.
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyClaimRefused>(kOtherPeer).size() == 1);
    session.Taken(cWolf);

    // Free again: the next claim holds.
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyClaim>(kHost).size() == 1);
}

TEST_CASE("Kill moves: crossed kill moves between two players, the host's first claim holds and neither changes owner", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Outbox.clear();

    session.Receive(kPeer, Claim(cPeerPlayer, cOtherPlayer, true));
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cPeerPlayer, true));
    CHECK(session.SentTo<NotifyClaim>(kOtherPeer).size() == 1);
    const auto cRefused = session.SentTo<NotifyClaimRefused>(kOtherPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->ForId == cOtherPlayer);
    CHECK(cRefused[0]->Reason == Payloads::Claim::kHeld);
    CHECK(OwnerSeen(session, cOtherPlayer, kPeer) == kOtherPeer);
}

TEST_CASE("Kill moves: a claim on an NPC that cannot pair (dead, its graph stopped) is refused as such", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.CannotPair.insert(cWolf);
    session.Outbox.clear();

    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    CHECK(session.SentTo<NotifyClaim>(kPeer).empty());
    const auto cRefused = session.SentTo<NotifyClaimRefused>(kPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->Reason == Payloads::Claim::kCannotTake);
    CHECK(OwnerSeen(session, cWolf, kPeer) == kHost);
}

TEST_CASE("Kill moves: a claim for an actor that is not the sender's, or on its own actor, is dropped", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cPeerPlayer, cWolf));
    session.Receive(kHost, Claim(cWolf, cWolf));
    CHECK(session.Outbox.empty());
}

TEST_CASE("Kill moves: a claimer gone gives the NPC back and frees its claims", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    session.Outbox.clear();

    session.Leave(kPeer);
    const auto cBack = session.SentTo<NotifyActorGiveBack>(kHost);
    REQUIRE(cBack.size() == 2);
    CHECK(std::ranges::any_of(cBack, [&](const auto* apBack) { return apBack->ActorId == cWolf && apBack->Taken && !apBack->Killed; }));
    CHECK(std::ranges::any_of(cBack, [&](const auto* apBack) { return apBack->Ended; }));
    CHECK(OwnerSeen(session, cWolf, kOtherPeer) == kHost);
    session.Taken(cWolf);
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyClaim>(kHost).size() == 1);
}

TEST_CASE("Kill moves: a player joining while an NPC is lent gets it, as the host's game has it", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    REQUIRE(OwnerSeen(session, cWolf, kHost) == kPeer);

    // The host's game lists only its own actors at a join; the lent wolf is a copy there.
    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    CHECK(std::ranges::any_of(session.SentTo<NotifyCharacterSpawn>(cLate), [&](const auto* apSpawn) { return apSpawn->SessionId == cWolf; }));
}
