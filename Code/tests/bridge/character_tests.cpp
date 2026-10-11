// HostCharacters: ids, owners, spawns, removal and respawn.
#include "session.h"

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

TEST_CASE("Player respawn: everyone else respawns its character", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    session.Receive(kPeer, PlayerRespawnRequest{});
    REQUIRE(session.SentTo<NotifyRespawn>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRespawn>(kHost)[0]->ActorId == cPeerPlayer);
    CHECK(session.SentTo<NotifyRespawn>(kPeer).empty());
}
