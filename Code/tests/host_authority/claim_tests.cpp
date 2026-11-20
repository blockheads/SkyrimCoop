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
