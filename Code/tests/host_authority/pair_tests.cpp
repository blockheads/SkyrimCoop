// HostPairs: pairs (kill moves) claimed through the host (one-simulator.md piece 4b).
#include "session.h"

namespace
{
PairClaimRequest Claim(uint32_t aActor, uint32_t aPartner)
{
    PairClaimRequest claim;
    claim.ActorId = aActor;
    claim.PartnerId = aPartner;
    return claim;
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

TEST_CASE("Pairs: the first claim on a host NPC holds, the NPC its claimer's game's until the pair is done", "[HostAuthority][pairs]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Outbox.clear();

    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    for (const ConnectionId_t cPlayer : {kHost, kPeer, kOtherPeer})
    {
        const auto cActorClaims = session.SentTo<NotifyActorClaim>(cPlayer);
        REQUIRE(cActorClaims.size() == 1);
        CHECK(cActorClaims[0]->ActorId == cWolf);
        CHECK(cActorClaims[0]->ForId == cPeerPlayer);
        CHECK(cActorClaims[0]->DeathOnGiveBack);
        const auto cClaims = session.SentTo<NotifyPairClaim>(cPlayer);
        REQUIRE(cClaims.size() == 1);
        CHECK(cClaims[0]->ActorId == cPeerPlayer);
        CHECK(cClaims[0]->PartnerId == cWolf);
    }
    CHECK(OwnerSeen(session, cWolf, kOtherPeer) == kPeer);

    // Another player's kill move on the same wolf within the round trip: refused as crossed, told to every game.
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyPairClaim>(kHost).empty());
    const auto cRefused = session.SentTo<NotifyPairRefused>(kOtherPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->Reason == Payloads::Pair::kCrossed);
    CHECK(session.SentTo<NotifyPairRefused>(kHost).size() == 1);

    // Every game first hears the wolf claimed for the claimer's game, its death kept for the give-back.
    // Done, then given back killed: the wolf is the host's again, its killer the claim's actor.
    session.Outbox.clear();
    PairDoneRequest done;
    done.ActorId = cPeerPlayer;
    done.PartnerId = cWolf;
    ActorGiveBackRequest giveBack;
    giveBack.ActorId = cWolf;
    giveBack.Tick = 4200;
    giveBack.Killed = true;
    session.Receive(kOtherPeer, done); // not its claim
    session.Receive(kOtherPeer, giveBack);
    CHECK(session.SentTo<NotifyPairDone>(kHost).empty());
    CHECK(session.SentTo<NotifyActorGiveBack>(kHost).empty());
    session.Receive(kPeer, done);
    session.Receive(kPeer, giveBack);
    const auto cDone = session.SentTo<NotifyPairDone>(kHost);
    REQUIRE(cDone.size() == 1);
    CHECK(cDone[0]->ActorId == cPeerPlayer);
    const auto cBack = session.SentTo<NotifyActorGiveBack>(kHost);
    REQUIRE(cBack.size() == 1);
    CHECK(cBack[0]->Tick == 4200);
    CHECK(cBack[0]->Killed);
    CHECK(cBack[0]->ForId == cPeerPlayer);
    CHECK(OwnerSeen(session, cWolf, kOtherPeer) == kHost);

    // Free again: the next claim holds.
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyPairClaim>(kHost).size() == 1);
}

TEST_CASE("Pairs: crossed kill moves between two players, the host's first claim holds and neither changes owner", "[HostAuthority][pairs]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Outbox.clear();

    session.Receive(kPeer, Claim(cPeerPlayer, cOtherPlayer));
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cPeerPlayer));
    CHECK(session.SentTo<NotifyPairClaim>(kOtherPeer).size() == 1);
    const auto cRefused = session.SentTo<NotifyPairRefused>(kOtherPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->ActorId == cOtherPlayer);
    CHECK(cRefused[0]->Reason == Payloads::Pair::kCrossed);
    CHECK(OwnerSeen(session, cOtherPlayer, kPeer) == kOtherPeer);
}

TEST_CASE("Pairs: a claim on an NPC that cannot pair (dead, its graph stopped) is refused as such", "[HostAuthority][pairs]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.CannotPair.insert(cWolf);
    session.Outbox.clear();

    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    CHECK(session.SentTo<NotifyPairClaim>(kPeer).empty());
    const auto cRefused = session.SentTo<NotifyPairRefused>(kPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->Reason == Payloads::Pair::kCannotPair);
    CHECK(OwnerSeen(session, cWolf, kPeer) == kHost);
}

TEST_CASE("Pairs: a claim for an actor that is not the sender's, or on its own actor, is dropped", "[HostAuthority][pairs]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cPeerPlayer, cWolf));
    session.Receive(kHost, Claim(cWolf, cWolf));
    CHECK(session.Outbox.empty());
}

TEST_CASE("Pairs: a claimer gone gives the NPC back and frees its claims", "[HostAuthority][pairs]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Receive(kPeer, Claim(cPeerPlayer, cWolf));
    session.Outbox.clear();

    session.Leave(kPeer);
    const auto cDone = session.SentTo<NotifyPairDone>(kHost);
    REQUIRE(cDone.size() == 1);
    CHECK(cDone[0]->PartnerId == cWolf);
    const auto cBack = session.SentTo<NotifyActorGiveBack>(kHost);
    REQUIRE(cBack.size() == 1);
    CHECK_FALSE(cBack[0]->Killed);
    CHECK(OwnerSeen(session, cWolf, kOtherPeer) == kHost);
    session.Outbox.clear();
    session.Receive(kOtherPeer, Claim(cOtherPlayer, cWolf));
    CHECK(session.SentTo<NotifyPairClaim>(kHost).size() == 1);
}

TEST_CASE("Pairs: a player joining while an NPC is lent gets it, as the host's game has it", "[HostAuthority][pairs]")
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
