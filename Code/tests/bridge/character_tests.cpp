// HostCharacters: ids, owners, spawns, removal and respawn.
#include "session.h"

#include <Messages/EnterCellRequest.h>

TEST_CASE("Characters: a player's own gets an id, everyone else a spawn, and it everyone else's", "[Bridge][Characters]")
{
    Session session;
    for (const uint32_t cServerId : {100u, 101u})
    {
        CharacterSpawnRequest spawn;
        spawn.ServerId = cServerId;
        session.Spawns.push_back(spawn);
    }
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Copies[cOtherPlayer] = glm::vec3(7.f, 8.f, 9.f);

    AssignCharacterRequest announce;
    announce.Cookie = 5;
    announce.ReferenceId = GameId(0, 0x14);
    announce.AppearanceBuffer = "face";
    session.Receive(kPeer, announce);

    const auto cResponses = session.SentTo<AssignCharacterResponse>(kPeer);
    REQUIRE(cResponses.size() == 1);
    CHECK(cResponses[0]->Cookie == 5);
    CHECK(cResponses[0]->Owner);
    const uint32_t cPlayer = cResponses[0]->ServerId;
    CHECK(cPlayer != cOtherPlayer);

    // The joiner: the host's actors and the other player as the host's copy shows it.
    const auto cToJoiner = session.SentTo<CharacterSpawnRequest>(kPeer);
    REQUIRE(cToJoiner.size() == 3);
    CHECK(cToJoiner[0]->ServerId == 100);
    CHECK(cToJoiner[1]->ServerId == 101);
    CHECK(cToJoiner[2]->ServerId == cOtherPlayer);
    CHECK(cToJoiner[2]->IsPlayer);
    CHECK(cToJoiner[2]->Position == session.Copies[cOtherPlayer]);

    // Everyone else: the new player, as it announced itself.
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cSpawns = session.SentTo<CharacterSpawnRequest>(cTo);
        REQUIRE(cSpawns.size() == 1);
        CHECK(cSpawns[0]->ServerId == cPlayer);
        CHECK(cSpawns[0]->AppearanceBuffer == "face");
    }
}

TEST_CASE("Characters: only the host announces anything but a player's own", "[Bridge][Characters]")
{
    Session session;
    AssignCharacterRequest announce;
    announce.ReferenceId = GameId(1, 0x1234);
    session.Receive(kPeer, announce);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Characters: removed by their owner, a cancel, or their player leaving", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    RequestRemoveCharacter removal;
    removal.ServerId = cHostNpc;
    session.Receive(kPeer, removal); // not its
    CHECK(session.Outbox.empty());
    session.Receive(kHost, removal);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kPeer)[0]->ServerId == cHostNpc);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost).empty());

    // Gone: the owner no longer speaks for it.
    session.Outbox.clear();
    DrawWeaponRequest draw;
    draw.Id = cHostNpc;
    session.Receive(kHost, draw);
    CHECK(session.Outbox.empty());

    // The host gave up an announcement it was still waiting on.
    const uint32_t cCancelled = session.Character(kHost);
    CancelAssignmentRequest cancel;
    cancel.Cookie = 3; // the third announcement in this session
    session.Receive(kHost, cancel);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kPeer)[0]->ServerId == cCancelled);

    session.Outbox.clear();
    session.Leave(kPeer);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost)[0]->ServerId == cPeerPlayer);
}

TEST_CASE("Respawn: the owner's reaches everyone; anyone else gets a fresh spawn", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    // A beast form: the owner sends its new appearance.
    RequestRespawn respawn;
    respawn.ActorId = cPeerPlayer;
    respawn.AppearanceBuffer = "werewolf";
    respawn.ChangeFlags = 7;
    session.Receive(kPeer, respawn);
    REQUIRE(session.SentTo<NotifyRespawn>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRespawn>(kOtherPeer)[0]->ActorId == cPeerPlayer);

    // The host dropped its copy and asks again: the new appearance, where the player last moved.
    ClientReferencesMoveRequest movement;
    movement.Updates[cPeerPlayer].UpdatedMovement.Position = glm::vec3(40.f, 50.f, 60.f);
    session.Receive(kPeer, movement);
    session.Outbox.clear();
    RequestRespawn again;
    again.ActorId = cPeerPlayer;
    session.Receive(kHost, again);
    const auto cSpawns = session.SentTo<CharacterSpawnRequest>(kHost);
    REQUIRE(cSpawns.size() == 1);
    CHECK(cSpawns[0]->AppearanceBuffer == "werewolf");
    CHECK(cSpawns[0]->ChangeFlags == 7);
    CHECK(cSpawns[0]->Position == movement.Updates[cPeerPlayer].UpdatedMovement.Position);
}

TEST_CASE("Respawn: a peer that lost the host's first character gets it again", "[Bridge][Characters]")
{
    Session session;
    // The first id handed out: the host's player, whose copy a peer drops when it changes cells.
    const uint32_t cHostPlayer = session.Character(kHost);
    REQUIRE(cHostPlayer == 0);
    CharacterSpawnRequest spawn;
    spawn.ServerId = cHostPlayer;
    session.Spawns.push_back(spawn);

    RequestRespawn request;
    request.ActorId = cHostPlayer;
    session.Receive(kPeer, request);
    const auto cSpawns = session.SentTo<CharacterSpawnRequest>(kPeer);
    REQUIRE(cSpawns.size() == 1);
    CHECK(cSpawns[0]->ServerId == cHostPlayer);
}

namespace
{
EnterCellRequest Arrival(uint32_t aCell, uint32_t aWorldSpace = 0, glm::vec3 aPosition = {})
{
    EnterCellRequest arrival;
    arrival.CellId = GameId(0, aCell);
    if (aWorldSpace)
        arrival.WorldSpaceId = GameId(0, aWorldSpace);
    arrival.Position = aPosition;
    return arrival;
}
} // namespace

TEST_CASE("Arriving: a player gets everyone with the host, and they get it", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    CharacterSpawnRequest npc;
    npc.ServerId = cHostNpc;
    session.Spawns.push_back(npc);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);

    // The host and the other peer are in Riverwood (an exterior) already.
    session.Receive(kHost, Arrival(0x100, 0x3C));
    session.Receive(kOtherPeer, Arrival(0x101, 0x3C));
    session.Outbox.clear();

    session.Receive(kPeer, Arrival(0x102, 0x3C, {1.f, 2.f, 3.f}));
    // It: the host's actors and the other player.
    const auto cToPeer = session.SentTo<CharacterSpawnRequest>(kPeer);
    REQUIRE(cToPeer.size() == 2);
    CHECK(cToPeer[0]->ServerId == cHostNpc);
    CHECK(cToPeer[1]->ServerId == cOtherPlayer);
    // Those there: it, where it arrived.
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cSpawns = session.SentTo<CharacterSpawnRequest>(cTo);
        REQUIRE(cSpawns.size() == 1);
        CHECK(cSpawns[0]->ServerId == cPeerPlayer);
        CHECK(cSpawns[0]->Position == glm::vec3(1.f, 2.f, 3.f));
    }
}

TEST_CASE("Arriving: the host's actors only go to players with the host", "[Bridge][Characters]")
{
    Session session;
    session.Character(kPeer);
    // The host went into an interior; the peer is still outside, loading.
    session.Receive(kHost, Arrival(0x200));
    session.Receive(kPeer, Arrival(0x102, 0x3C));
    session.Outbox.clear();

    const uint32_t cHostNpc = session.Character(kHost);
    CHECK(session.SentTo<CharacterSpawnRequest>(kPeer).empty());

    CharacterSpawnRequest npc;
    npc.ServerId = cHostNpc;
    session.Spawns.push_back(npc);
    session.Receive(kPeer, Arrival(0x200));
    const auto cToPeer = session.SentTo<CharacterSpawnRequest>(kPeer);
    REQUIRE(cToPeer.size() == 1);
    CHECK(cToPeer[0]->ServerId == cHostNpc);
}

namespace
{
// Both players announce in QASmoke (an interior), as joining does; the peer's player's id.
uint32_t BothInQASmoke(Session& aSession)
{
    AssignCharacterRequest player;
    player.Cookie = 1;
    player.ReferenceId = GameId(0, 0x14);
    player.CellId = GameId(0, 0x32AE7);
    aSession.Receive(kHost, player);
    // The host's player, as its engine shows it.
    CharacterSpawnRequest hostPlayer;
    hostPlayer.ServerId = aSession.SentTo<AssignCharacterResponse>(kHost)[0]->ServerId;
    aSession.Spawns.push_back(hostPlayer);
    aSession.Receive(kPeer, player);
    const uint32_t cPeerPlayer = aSession.SentTo<AssignCharacterResponse>(kPeer)[0]->ServerId;
    aSession.Outbox.clear();
    return cPeerPlayer;
}
} // namespace

TEST_CASE("Arriving: the host first, then a peer still on its way", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = BothInQASmoke(session);

    // The host reaches Riverwood first: the peer is still in QASmoke, it gets nothing yet.
    session.Receive(kHost, Arrival(0x9732, 0x3C));
    CHECK(session.SentTo<CharacterSpawnRequest>(kHost).empty());
    CHECK(session.SentTo<CharacterSpawnRequest>(kPeer).empty());
    // The peer arrives: the host gets it, once.
    session.Receive(kPeer, Arrival(0x9732, 0x3C));
    const auto cToHost = session.SentTo<CharacterSpawnRequest>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->ServerId == cPeerPlayer);
}

TEST_CASE("Arriving: a peer ahead of the host gets everyone when the host arrives", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = BothInQASmoke(session);

    // The peer reaches Riverwood first: nothing yet, either way.
    session.Receive(kPeer, Arrival(0x9732, 0x3C));
    CHECK(session.Outbox.empty());
    // The host arrives: it gets the peer, the peer gets it.
    session.Receive(kHost, Arrival(0x9733, 0x3C));
    const auto cToHost = session.SentTo<CharacterSpawnRequest>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->ServerId == cPeerPlayer);
    const auto cToPeer = session.SentTo<CharacterSpawnRequest>(kPeer);
    REQUIRE(cToPeer.size() == 1);
    CHECK(cToPeer[0]->ServerId == session.Spawns[0].ServerId);
}

TEST_CASE("Arriving: others spawn a player as it arrives, not as it joined", "[Bridge][Characters]")
{
    Session session;
    BothInQASmoke(session);

    // The peer changed its equipment since it joined, then both go to Riverwood.
    session.Receive(kHost, Arrival(0x9732, 0x3C));
    auto arrival = Arrival(0x9732, 0x3C);
    Inventory::Entry sword;
    sword.BaseId = GameId(0, 0x12EB7);
    sword.Count = 1;
    sword.ExtraWorn = true;
    arrival.Data.InitialInventory.Entries.push_back(sword);
    arrival.Data.IsWeaponDrawn = true;
    session.Receive(kPeer, arrival);

    const auto cToHost = session.SentTo<CharacterSpawnRequest>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->InventoryContent == arrival.Data.InitialInventory);
    CHECK(cToHost[0]->IsWeaponDrawn);
}

TEST_CASE("Player respawn: everyone else respawns its character", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    session.Receive(kPeer, PlayerRespawnRequest{});
    REQUIRE(session.SentTo<NotifyRespawn>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRespawn>(kHost)[0]->ActorId == cPeerPlayer);
    CHECK(session.SentTo<NotifyRespawn>(kPeer).empty());
}
