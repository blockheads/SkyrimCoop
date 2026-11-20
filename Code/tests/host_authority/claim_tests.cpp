// HostClaims: one holder per host actor, whatever the feature (one-simulator.md, R4).
#include "session.h"

namespace
{
MountRequest Ride(uint32_t aRider, uint32_t aMount)
{
    MountRequest ride;
    ride.RiderId = aRider;
    ride.MountId = aMount;
    return ride;
}

PairClaimRequest KillMove(uint32_t aActor, uint32_t aPartner)
{
    PairClaimRequest claim;
    claim.ActorId = aActor;
    claim.PartnerId = aPartner;
    return claim;
}

// Whose game moves aActor: its owner's movement goes to the others.
bool MovedBy(Session& aSession, uint32_t aActor, ConnectionId_t aMover, ConnectionId_t aWatcher)
{
    aSession.Outbox.clear();
    ReferencesMoveRequest moved;
    moved.Updates[aActor] = {};
    aSession.Receive(aMover, moved);
    return !aSession.SentTo<NotifyReferencesMove>(aWatcher).empty();
}
} // namespace

TEST_CASE("Claims: one holder whatever the feature: a ridden horse takes no kill move, one in a kill move no rider", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);

    session.Receive(kPeer, Ride(cPeerPlayer, cHorse));
    session.Outbox.clear();
    session.Receive(kOtherPeer, KillMove(cOtherPlayer, cHorse));
    REQUIRE(session.SentTo<NotifyPairRefused>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorClaim>(kHost).empty());

    // The host's own player on its horse holds it too: the host's horse is not lent from under it.
    Session hosted;
    const uint32_t cHostPlayer = hosted.Character(kHost);
    const uint32_t cMare = hosted.Character(kHost);
    const uint32_t cGuest = hosted.Character(kPeer);
    hosted.Receive(kHost, Ride(cHostPlayer, cMare));
    hosted.Outbox.clear();
    hosted.Receive(kPeer, KillMove(cGuest, cMare));
    REQUIRE(hosted.SentTo<NotifyPairRefused>(kPeer).size() == 1);
    CHECK(MovedBy(hosted, cMare, kHost, kPeer));

    // A wolf in a kill move: nobody rides it.
    const uint32_t cWolf = session.Character(kHost);
    session.Receive(kOtherPeer, KillMove(cOtherPlayer, cWolf));
    session.Outbox.clear();
    session.Receive(kPeer, Ride(cPeerPlayer, cWolf));
    CHECK(session.SentTo<NotifyMountRefused>(kPeer).size() == 1);
}

TEST_CASE("Claims: a give-back after the claim passed to a passenger is not the holder's word", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Receive(kPeer, Ride(cPeerPlayer, cHorse));
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
    const auto cClaims = session.SentTo<NotifyActorClaim>(kHost);
    REQUIRE(cClaims.size() == 1);
    CHECK(cClaims[0]->ForId == cOtherPlayer);

    // Its game's give-back, after its last update, comes too late to count.
    session.Outbox.clear();
    ActorGiveBackRequest late;
    late.ActorId = cHorse;
    session.Receive(kPeer, late);
    CHECK(session.SentTo<NotifyActorGiveBack>(kHost).empty());
    CHECK(MovedBy(session, cHorse, kOtherPeer, kHost));
}

TEST_CASE("Claims: one game cannot hold one horse for two features, and an older claim's give-back is not this one's", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    // Held for its player's kill move: the same player's ride is another claim, refused.
    session.Receive(kPeer, KillMove(cPeerPlayer, cHorse));
    session.Outbox.clear();
    session.Receive(kPeer, Ride(cPeerPlayer, cHorse));
    CHECK(session.SentTo<NotifyMountRefused>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorClaim>(kHost).empty());

    // Given back (a ride's "killed" would not count: only a claim keeping its death has one), taken, ridden.
    PairDoneRequest done;
    done.ActorId = cPeerPlayer;
    done.PartnerId = cHorse;
    session.Receive(kPeer, done);
    ActorGiveBackRequest back;
    back.ActorId = cHorse;
    back.ClaimId = 1;
    session.Receive(kPeer, back);
    session.Taken(cHorse);
    session.Outbox.clear();
    session.Receive(kPeer, Ride(cPeerPlayer, cHorse));
    const auto cRide = session.SentTo<NotifyActorClaim>(kHost);
    REQUIRE(cRide.size() == 1);
    CHECK(cRide[0]->ClaimId == 2);
    CHECK_FALSE(cRide[0]->DeathOnGiveBack);

    // A late copy of the first claim's give-back: dropped. The ride's own, saying killed: back alive.
    session.Outbox.clear();
    session.Receive(kPeer, back);
    CHECK(session.SentTo<NotifyActorGiveBack>(kHost).empty());
    back.ClaimId = 2;
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
    session.Receive(kPeer, Ride(cPeerPlayer, cHorse));
    session.Outbox.clear();
    ActorGiveBackRequest back;
    back.ActorId = cHorse;
    back.ClaimId = 1;
    session.Receive(kPeer, back);
    REQUIRE(session.SentTo<NotifyDismount>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyDismount>(kOtherPeer)[0]->RiderId == cPeerPlayer);
    session.Taken(cHorse);
    session.Outbox.clear();
    session.Receive(kOtherPeer, Ride(cOtherPlayer, cHorse));
    CHECK(session.SentTo<NotifyMountRefused>(kOtherPeer).empty());
}

TEST_CASE("Claims: a player joining while an actor is on its way back gets the host's copy of it", "[HostAuthority][claims]")
{
    Session session;
    const uint32_t cWolf = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Receive(kPeer, KillMove(cPeerPlayer, cWolf));
    ActorGiveBackRequest back;
    back.ActorId = cWolf;
    back.ClaimId = 1;
    session.Receive(kPeer, back);

    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    CHECK(std::ranges::any_of(session.SentTo<NotifyCharacterSpawn>(cLate), [&](const auto* apSpawn) { return apSpawn->SessionId == cWolf; }));
}
