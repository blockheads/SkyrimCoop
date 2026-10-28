// HostCharacters: ids, owners, spawns, removal and respawn.
#include "session.h"

#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyPartyTravel.h>

TEST_CASE("Characters: a player's own goes under the id from its join, everyone else gets a spawn, and it everyone else's", "[HostAuthority][Characters]")
{
    Session session;
    for (const uint32_t cSessionId : {100u, 101u})
    {
        NotifyCharacterSpawn spawn;
        spawn.SessionId = cSessionId;
        session.Spawns.push_back(spawn);
    }
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);
    session.Copies[cOtherPlayer] = glm::vec3(7.f, 8.f, 9.f);

    AnnounceCharacterRequest announce;
    announce.SessionId = session.CharacterId(kPeer);
    announce.ReferenceId = GameId(0, 0x14);
    announce.AppearanceBuffer = "face";
    announce.Weight = 100.f;
    announce.EffectIds = {GameId(0, 0x4B2D9)};
    session.Receive(kPeer, announce);
    const uint32_t cPlayer = announce.SessionId;
    CHECK(cPlayer != cOtherPlayer);

    // The joiner: the host's actors and the other player as the host's copy shows it.
    const auto cToJoiner = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cToJoiner.size() == 3);
    CHECK(cToJoiner[0]->SessionId == 100);
    CHECK(cToJoiner[1]->SessionId == 101);
    CHECK(cToJoiner[2]->SessionId == cOtherPlayer);
    CHECK(cToJoiner[2]->IsPlayer);
    CHECK(cToJoiner[2]->Position == session.Copies[cOtherPlayer]);

    // Everyone else: the new player, as it announced itself.
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(cTo);
        REQUIRE(cSpawns.size() == 1);
        CHECK(cSpawns[0]->SessionId == cPlayer);
        CHECK(cSpawns[0]->AppearanceBuffer == "face");
        CHECK(cSpawns[0]->Weight == 100.f);
        CHECK(cSpawns[0]->EffectIds == announce.EffectIds);
    }
}

TEST_CASE("Characters: only the host announces anything but a player's own", "[HostAuthority][Characters]")
{
    Session session;
    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(1, 0x1234);
    session.Receive(kPeer, announce);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Characters: only under an id the host gave", "[HostAuthority][Characters]")
{
    Session session;
    // A player under another player's character's id.
    AnnounceCharacterRequest player;
    player.ReferenceId = GameId(0, 0x14);
    player.SessionId = session.CharacterId(kOtherPeer);
    session.Receive(kPeer, player);
    CHECK(session.Outbox.empty());

    // The host's game under an id it was not given, or a player's.
    AnnounceCharacterRequest npc;
    npc.ReferenceId = GameId(1, 0x1234);
    for (const uint32_t cId : {0u, 9999u, session.CharacterId(kPeer)})
    {
        npc.SessionId = cId;
        session.Receive(kHost, npc);
    }
    CHECK(session.Outbox.empty());

    // The same id twice: the second goes nowhere.
    const uint32_t cNpc = session.Character(kHost);
    npc.SessionId = cNpc;
    session.Receive(kHost, npc);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Characters: removed by their owner or their player leaving", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    RequestRemoveCharacter removal;
    removal.SessionId = cHostNpc;
    session.Receive(kPeer, removal); // not its
    CHECK(session.Outbox.empty());
    session.Receive(kHost, removal);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kPeer)[0]->SessionId == cHostNpc);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost).empty());

    // Gone: the owner no longer speaks for it.
    session.Outbox.clear();
    DrawWeaponRequest draw;
    draw.Id = cHostNpc;
    session.Receive(kHost, draw);
    CHECK(session.Outbox.empty());

    session.Outbox.clear();
    session.Leave(kPeer);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost)[0]->SessionId == cPeerPlayer);
}

TEST_CASE("Characters: the owner's removal says where its actor went, for the copies to follow", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cMoved = session.Character(kHost);
    const uint32_t cDisabled = session.Character(kHost);

    RequestRemoveCharacter removal;
    removal.SessionId = cMoved;
    removal.Whereabouts.Where = ActorWhereabouts::State::kPlaced;
    removal.Whereabouts.CellId = GameId(0, 0x165A7); // an interior: no worldspace
    removal.Whereabouts.Position = {100.f, -200.f, 64.f};
    removal.Whereabouts.Heading = 1.5f;
    session.Receive(kHost, removal);
    removal.SessionId = cDisabled;
    removal.Whereabouts = {};
    removal.Whereabouts.Where = ActorWhereabouts::State::kDisabled;
    session.Receive(kHost, removal);

    const auto cSent = session.SentTo<NotifyRemoveCharacter>(kPeer);
    REQUIRE(cSent.size() == 2);
    CHECK(cSent[0]->SessionId == cMoved);
    CHECK(cSent[0]->Whereabouts.Where == ActorWhereabouts::State::kPlaced);
    CHECK(cSent[0]->Whereabouts.CellId == GameId(0, 0x165A7));
    CHECK(cSent[0]->Whereabouts.WorldSpaceId == GameId{});
    CHECK(cSent[0]->Whereabouts.Position.x == 100.f);
    CHECK(cSent[0]->Whereabouts.Heading == 1.5f);
    CHECK(cSent[1]->Whereabouts.Where == ActorWhereabouts::State::kDisabled);

    // A player leaving: nothing to follow, its copies go.
    session.Outbox.clear();
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Outbox.clear();
    session.Leave(kPeer);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost)[0]->SessionId == cPeerPlayer);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost)[0]->Whereabouts.Where == ActorWhereabouts::State::kUnknown);
}

TEST_CASE("Respawn: the owner's reaches everyone; anyone else gets a fresh spawn", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    // New looks (a race, the race menu): the owner sends its appearance, its tints and its weight.
    RequestRespawn respawn;
    respawn.ActorId = cPeerPlayer;
    respawn.AppearanceBuffer = "werewolf";
    respawn.ChangeFlags = 7;
    respawn.FaceTints.Entries.push_back({"SkinTone", 1.f, 0x101010, 6});
    respawn.Weight = 0.f;
    session.Receive(kPeer, respawn);
    REQUIRE(session.SentTo<NotifyRespawn>(kHost).size() == 1);
    const auto* cpLooks = session.SentTo<NotifyRespawn>(kOtherPeer)[0];
    CHECK(cpLooks->ActorId == cPeerPlayer);
    CHECK(cpLooks->AppearanceBuffer == respawn.AppearanceBuffer);
    CHECK(cpLooks->ChangeFlags == respawn.ChangeFlags);
    CHECK(cpLooks->FaceTints == respawn.FaceTints);
    CHECK(cpLooks->Weight == respawn.Weight);

    // The host dropped its copy and asks again: the new appearance, where the player last moved.
    ReferencesMoveRequest movement;
    movement.Updates[cPeerPlayer].UpdatedMovement.Position = glm::vec3(40.f, 50.f, 60.f);
    session.Receive(kPeer, movement);
    session.Outbox.clear();
    RequestRespawn again;
    again.ActorId = cPeerPlayer;
    session.Receive(kHost, again);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(kHost);
    REQUIRE(cSpawns.size() == 1);
    CHECK(cSpawns[0]->AppearanceBuffer == "werewolf");
    CHECK(cSpawns[0]->ChangeFlags == 7);
    CHECK(cSpawns[0]->FaceTints == respawn.FaceTints);
    CHECK(cSpawns[0]->Weight == 0.f);
    CHECK(cSpawns[0]->Position == movement.Updates[cPeerPlayer].UpdatedMovement.Position);
}

TEST_CASE("Respawn: the fresh spawn starts from the owner's state at its respawn, else the host's copy's", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    session.Copies[cPeerPlayer] = glm::vec3(1.f, 2.f, 3.f);
    // The host drops its copy before it asks (CharacterService::RespawnCopy).
    const auto cRespawnAgain = [&] {
        session.Copies.erase(cPeerPlayer);
        session.Outbox.clear();
        RequestRespawn again;
        again.ActorId = cPeerPlayer;
        session.Receive(kHost, again);
        const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(kHost);
        REQUIRE(cSpawns.size() == 1);
        return *cSpawns[0];
    };

    RequestRespawn respawn;
    respawn.ActorId = cPeerPlayer;
    session.Receive(kPeer, respawn);
    CHECK(cRespawnAgain().Position == glm::vec3(1.f, 2.f, 3.f));

    // A beast form: the owner's new graph and its values come along.
    respawn.HasState = true;
    respawn.GraphState = "werewolf graph";
    respawn.LatestAction.EventName = "IdleWerewolfTransformation";
    respawn.Data.InitialActorValues.ActorMaxValuesList[24] = 5000.f;
    session.Receive(kPeer, respawn);
    const auto cSpawn = cRespawnAgain();
    CHECK(cSpawn.GraphState == "werewolf graph");
    CHECK(cSpawn.LatestAction.EventName == "IdleWerewolfTransformation");
    CHECK(cSpawn.InitialActorValues.ActorMaxValuesList.at(24) == 5000.f);
}

TEST_CASE("Respawn: a peer that lost the host's first character gets it again", "[HostAuthority][Characters]")
{
    Session session;
    // The first id handed out (never 0, which means no character): the host's player, whose copy a peer drops
    // when it changes cells.
    const uint32_t cHostPlayer = session.Character(kHost);
    REQUIRE(cHostPlayer != 0);
    NotifyCharacterSpawn spawn;
    spawn.SessionId = cHostPlayer;
    session.Spawns.push_back(spawn);

    RequestRespawn request;
    request.ActorId = cHostPlayer;
    session.Receive(kPeer, request);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cSpawns.size() == 1);
    CHECK(cSpawns[0]->SessionId == cHostPlayer);
}

namespace
{
// An arrival from before joining (taking nobody along), the party tether's, or the player's own travel.
EnterCellRequest Arrival(uint32_t aCell, uint32_t aWorldSpace = 0, glm::vec3 aPosition = {},
                         EnterCellRequest::Travel aHow = EnterCellRequest::Travel::kBeforeJoining)
{
    EnterCellRequest arrival;
    arrival.How = aHow;
    arrival.CellId = GameId(0, aCell);
    if (aWorldSpace)
        arrival.WorldSpaceId = GameId(0, aWorldSpace);
    arrival.Position = aPosition;
    arrival.Heading = 1.5f;
    return arrival;
}
} // namespace

TEST_CASE("Arriving: a player gets the host's actors there; players' copies travel with the party", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    NotifyCharacterSpawn npc;
    npc.SessionId = cHostNpc;
    session.Spawns.push_back(npc);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);

    // The host and the other peer are in Riverwood (an exterior) already.
    session.Receive(kHost, Arrival(0x100, 0x3C));
    session.Receive(kOtherPeer, Arrival(0x101, 0x3C));
    session.Outbox.clear();

    session.Receive(kPeer, Arrival(0x102, 0x3C, {1.f, 2.f, 3.f}));
    // It: the host's actors, not the other player (its copy came along).
    const auto cToPeer = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cToPeer.size() == 1);
    CHECK(cToPeer[0]->SessionId == cHostNpc);
    // Those there: nothing (their copy of it came along).
    for (const auto cTo : {kHost, kOtherPeer})
        CHECK(session.SentTo<NotifyCharacterSpawn>(cTo).empty());
    (void)cPeerPlayer;
    (void)cOtherPlayer;
}

TEST_CASE("Arriving: the host's actors only go to players with the host", "[HostAuthority][Characters]")
{
    Session session;
    session.Character(kPeer);
    // The host went into an interior; the peer is still outside, loading.
    session.Receive(kHost, Arrival(0x200));
    session.Receive(kPeer, Arrival(0x102, 0x3C));
    session.Outbox.clear();

    const uint32_t cHostNpc = session.Character(kHost);
    CHECK(session.SentTo<NotifyCharacterSpawn>(kPeer).empty());

    NotifyCharacterSpawn npc;
    npc.SessionId = cHostNpc;
    session.Spawns.push_back(npc);
    session.Receive(kPeer, Arrival(0x200));
    const auto cToPeer = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cToPeer.size() == 1);
    CHECK(cToPeer[0]->SessionId == cHostNpc);
}

namespace
{
// Both players announce in QASmoke (an interior), as joining does; the peer's player's id.
uint32_t BothInQASmoke(Session& aSession)
{
    AnnounceCharacterRequest player;
    player.SessionId = aSession.CharacterId(kHost);
    player.ReferenceId = GameId(0, 0x14);
    player.CellId = GameId(0, 0x32AE7);
    aSession.Receive(kHost, player);
    // The host's player, as its engine shows it.
    NotifyCharacterSpawn hostPlayer;
    hostPlayer.SessionId = aSession.CharacterId(kHost);
    aSession.Spawns.push_back(hostPlayer);
    player.SessionId = aSession.CharacterId(kPeer);
    aSession.Receive(kPeer, player);
    const uint32_t cPeerPlayer = aSession.CharacterId(kPeer);
    aSession.Outbox.clear();
    return cPeerPlayer;
}
} // namespace

TEST_CASE("Arriving: the host first, then a peer still on its way", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = BothInQASmoke(session);

    // The host reaches Riverwood first: the peer is still in QASmoke, it gets nothing yet.
    session.Receive(kHost, Arrival(0x9732, 0x3C));
    CHECK(session.SentTo<NotifyCharacterSpawn>(kHost).empty());
    CHECK(session.SentTo<NotifyCharacterSpawn>(kPeer).empty());
    // The peer arrives: its copy on the host came along, nothing is spawned for it.
    session.Receive(kPeer, Arrival(0x9732, 0x3C));
    CHECK(session.SentTo<NotifyCharacterSpawn>(kHost).empty());
    (void)cPeerPlayer;
}

TEST_CASE("Arriving: a peer ahead of the host gets the host's actors when the host arrives", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = BothInQASmoke(session);

    // The peer reaches Riverwood first: nothing yet, either way.
    session.Receive(kPeer, Arrival(0x9732, 0x3C));
    CHECK(session.Outbox.empty());
    // The host arrives with an NPC (a follower): the peer gets it, not the host's player (its copy came along).
    NotifyCharacterSpawn follower;
    follower.SessionId = session.Character(kHost);
    session.Spawns.push_back(follower);
    session.Receive(kHost, Arrival(0x9733, 0x3C));
    CHECK(session.SentTo<NotifyCharacterSpawn>(kHost).empty());
    const auto cToPeer = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cToPeer.size() == 1);
    CHECK(cToPeer[0]->SessionId == follower.SessionId);
    // Once: the host's next arrival with the peer there sends it nothing more.
    session.Outbox.clear();
    session.Receive(kHost, Arrival(0x9734, 0x3C));
    CHECK(session.SentTo<NotifyCharacterSpawn>(kPeer).empty());
    (void)cPeerPlayer;
}

TEST_CASE("Party tether: a player's own travel takes everyone else along, the host too", "[HostAuthority][Characters]")
{
    Session session;
    BothInQASmoke(session);

    session.Receive(kPeer, Arrival(0x9732, 0x3C, {1.f, 2.f, 3.f}, EnterCellRequest::Travel::kOwn));
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cTravels = session.SentTo<NotifyPartyTravel>(cTo);
        REQUIRE(cTravels.size() == 1);
        CHECK(cTravels[0]->CellId == GameId(0, 0x9732));
        CHECK(cTravels[0]->WorldSpaceId == GameId(0, 0x3C));
        CHECK(cTravels[0]->Position == glm::vec3(1.f, 2.f, 3.f));
        CHECK(cTravels[0]->Heading == 1.5f); // followers face the way the leader arrived
    }

    // The host leads into an interior: the peers follow.
    session.Outbox.clear();
    session.Receive(kHost, Arrival(0x200, 0, {}, EnterCellRequest::Travel::kOwn));
    CHECK(session.SentTo<NotifyPartyTravel>(kHost).empty());
    for (const auto cTo : {kPeer, kOtherPeer})
    {
        const auto cTravels = session.SentTo<NotifyPartyTravel>(cTo);
        REQUIRE(cTravels.size() == 1);
        CHECK(cTravels[0]->CellId == GameId(0, 0x200));
        CHECK(!cTravels[0]->WorldSpaceId);
    }
}

TEST_CASE("Party tether: a player joining away from the host is sent to it", "[HostAuthority][Characters]")
{
    Session session;
    AnnounceCharacterRequest host;
    host.SessionId = session.CharacterId(kHost);
    host.ReferenceId = GameId(0, 0x14);
    host.CellId = GameId(0, 0x32AE7);
    host.Position = glm::vec3(1.f, 2.f, 3.f);
    host.Rotation.y = 0.5f;
    session.Receive(kHost, host);

    // One joins in Riverwood: it is sent to the host's player in QASmoke, facing the way it does.
    AnnounceCharacterRequest away = host;
    away.SessionId = session.CharacterId(kPeer);
    away.CellId = GameId(0, 0x9732);
    away.WorldSpaceId = GameId(0, 0x3C);
    session.Receive(kPeer, away);
    const auto cTravels = session.SentTo<NotifyPartyTravel>(kPeer);
    REQUIRE(cTravels.size() == 1);
    CHECK(cTravels[0]->CellId == GameId(0, 0x32AE7));
    CHECK(!cTravels[0]->WorldSpaceId);
    CHECK(cTravels[0]->Position == glm::vec3(1.f, 2.f, 3.f));
    CHECK(cTravels[0]->Heading == Approx(0.5f).margin(0.001f)); // quantized on the wire
    CHECK(session.SentTo<NotifyPartyTravel>(kHost).empty());

    // One joining where the host is stays.
    AnnounceCharacterRequest here = host;
    here.SessionId = session.CharacterId(kOtherPeer);
    session.Receive(kOtherPeer, here);
    CHECK(session.SentTo<NotifyPartyTravel>(kOtherPeer).empty());
}

TEST_CASE("Party tether: a player brought to the party gets the players' characters again where it arrives",
          "[HostAuthority][Characters]")
{
    Session session;
    AnnounceCharacterRequest host;
    host.SessionId = session.CharacterId(kHost);
    host.ReferenceId = GameId(0, 0x14);
    host.CellId = GameId(0, 0x32AE7);
    session.Receive(kHost, host);
    const uint32_t cHostPlayer = session.CharacterId(kHost);
    NotifyCharacterSpawn hostSpawn;
    hostSpawn.SessionId = cHostPlayer;
    session.Spawns.push_back(hostSpawn);

    // It joins in Riverwood and is sent to QASmoke; the copies made at its join may be gone by its arrival.
    AnnounceCharacterRequest away = host;
    away.SessionId = session.CharacterId(kPeer);
    away.CellId = GameId(0, 0x9732);
    away.WorldSpaceId = GameId(0, 0x3C);
    session.Receive(kPeer, away);
    const uint32_t cPeerPlayer = session.CharacterId(kPeer);
    session.Outbox.clear();

    session.Receive(kPeer, Arrival(0x32AE7, 0, {1.f, 2.f, 3.f}, EnterCellRequest::Travel::kTether));
    const auto cToPeer = session.SentTo<NotifyCharacterSpawn>(kPeer);
    CHECK(std::ranges::any_of(cToPeer, [&](const auto* apSpawn) { return apSpawn->SessionId == cHostPlayer; }));
    const auto cToHost = session.SentTo<NotifyCharacterSpawn>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->SessionId == cPeerPlayer);
    CHECK(cToHost[0]->Position == glm::vec3(1.f, 2.f, 3.f));

    // Its next arrival is an ordinary one: the players' copies travel with the party.
    session.Outbox.clear();
    session.Receive(kPeer, Arrival(0x32AE7, 0, {}, EnterCellRequest::Travel::kTether));
    CHECK(session.SentTo<NotifyCharacterSpawn>(kHost).empty());
}

TEST_CASE("Party tether: a player brought to the party outdoors gets them where it arrives, not where it joined",
          "[HostAuthority][Characters]")
{
    Session session;
    AnnounceCharacterRequest host;
    host.SessionId = session.CharacterId(kHost);
    host.ReferenceId = GameId(0, 0x14);
    host.CellId = GameId(0, 0x9732);
    host.WorldSpaceId = GameId(0, 0x3C);
    host.Position = glm::vec3(100.f, 100.f, 0.f);
    session.Receive(kHost, host);

    // Four cells off in Tamriel, as Skyrim orders it: its assignment, then where it was as it joined (the same
    // worldspace: "with the host"), then its arrival by the party's travel.
    AnnounceCharacterRequest away = host;
    away.SessionId = session.CharacterId(kPeer);
    away.CellId = GameId(0, 0x9740);
    away.Position = glm::vec3(4 * 4096.f + 100.f, 100.f, 0.f);
    session.Receive(kPeer, away);
    const uint32_t cPeerPlayer = session.CharacterId(kPeer);
    session.Receive(kPeer, Arrival(0x9740, 0x3C, {4 * 4096.f + 100.f, 100.f, 0.f}));
    session.Outbox.clear();

    session.Receive(kPeer, Arrival(0x9732, 0x3C, {101.f, 100.f, 0.f}, EnterCellRequest::Travel::kTether));
    const auto cToHost = session.SentTo<NotifyCharacterSpawn>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->SessionId == cPeerPlayer);
    CHECK(cToHost[0]->Position == glm::vec3(101.f, 100.f, 0.f));
}

TEST_CASE("Party tether: a player joining outdoors out of the host's grid is sent to it", "[HostAuthority][Characters]")
{
    Session session;
    AnnounceCharacterRequest host;
    host.SessionId = session.CharacterId(kHost);
    host.ReferenceId = GameId(0, 0x14);
    host.CellId = GameId(0, 0x9732);
    host.WorldSpaceId = GameId(0, 0x3C);
    host.Position = glm::vec3(100.f, 100.f, 0.f);
    session.Receive(kHost, host);

    // In Tamriel too, but four cells (4096 units each) away: sent to the host's player.
    AnnounceCharacterRequest away = host;
    away.SessionId = session.CharacterId(kPeer);
    away.CellId = GameId(0, 0x9740);
    away.Position = glm::vec3(4 * 4096.f + 100.f, 100.f, 0.f);
    session.Receive(kPeer, away);
    const auto cTravels = session.SentTo<NotifyPartyTravel>(kPeer);
    REQUIRE(cTravels.size() == 1);
    CHECK(cTravels[0]->WorldSpaceId == GameId(0, 0x3C));
    CHECK(cTravels[0]->Position == glm::vec3(100.f, 100.f, 0.f));

    // Two cells away, in its loaded grid: stays.
    AnnounceCharacterRequest near = host;
    near.SessionId = session.CharacterId(kOtherPeer);
    near.Position = glm::vec3(2 * 4096.f + 100.f, 100.f, 0.f);
    session.Receive(kOtherPeer, near);
    CHECK(session.SentTo<NotifyPartyTravel>(kOtherPeer).empty());
}

TEST_CASE("Party tether: a follower's or a joiner's arrival takes nobody along", "[HostAuthority][Characters]")
{
    Session session;
    BothInQASmoke(session);

    for (const auto cHow : {EnterCellRequest::Travel::kTether, EnterCellRequest::Travel::kBeforeJoining})
    {
        session.Receive(kPeer, Arrival(0x9732, 0x3C, {}, cHow));
        for (const auto cTo : {kHost, kPeer, kOtherPeer})
            CHECK(session.SentTo<NotifyPartyTravel>(cTo).empty());
    }
}

TEST_CASE("Party tether: a follower that arrives where the party is not is sent on", "[HostAuthority][Characters]")
{
    Session session;
    BothInQASmoke(session);

    // Two players lead at once: the peer into the inn, then the other peer to Riverwood. The peer follows
    // the other one's travel to Riverwood; the other peer, still on its way, follows the peer into the inn.
    session.Receive(kPeer, Arrival(0x133C6, 0, {}, EnterCellRequest::Travel::kOwn));
    session.Receive(kOtherPeer, Arrival(0x9732, 0x3C, {5.f, 6.f, 7.f}, EnterCellRequest::Travel::kOwn));
    session.Outbox.clear();
    session.Receive(kOtherPeer, Arrival(0x133C6, 0, {}, EnterCellRequest::Travel::kTether));
    const auto cOn = session.SentTo<NotifyPartyTravel>(kOtherPeer);
    REQUIRE(cOn.size() == 1);
    CHECK(cOn[0]->CellId == GameId(0, 0x9732));
    CHECK(cOn[0]->Position == glm::vec3(5.f, 6.f, 7.f));
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());

    // Where the party went (outdoors: within the loaded grid of it): nothing more.
    session.Outbox.clear();
    session.Receive(kPeer, Arrival(0x9733, 0x3C, {100.f, 6.f, 7.f}, EnterCellRequest::Travel::kTether));
    CHECK(session.SentTo<NotifyPartyTravel>(kPeer).empty());
}

TEST_CASE("Arriving: a fresh spawn of a player is as it arrived, not as it joined", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = BothInQASmoke(session);

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

    // The host's copy of it was lost: the host's game asks for it again (RunPresence).
    RequestRespawn again;
    again.ActorId = cPeerPlayer;
    session.Receive(kHost, again);
    const auto cToHost = session.SentTo<NotifyCharacterSpawn>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->InventoryContent == arrival.Data.InitialInventory);
    CHECK(cToHost[0]->IsWeaponDrawn);
}

TEST_CASE("Names: actions and face tints arrive as sent, with nothing shared first", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    // A tinted NPC of the host's, announced as a player joins: its spawn carries the texture names.
    AnnounceCharacterRequest announce;
    announce.SessionId = session.NewCharacterId();
    announce.ReferenceId = GameId(1, 0x2000);
    Tints::Entry tint;
    tint.Name = "Actors\\Character\\Character Assets\\TintMasks\\SkinTone.dds";
    tint.Alpha = 0.5f;
    tint.Color = 0x00C8A080;
    tint.Type = 6;
    announce.FaceTints.Entries.push_back(tint);
    session.Receive(kHost, announce);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cSpawns.size() == 1);
    REQUIRE(cSpawns[0]->FaceTints.Entries.size() == 1);
    CHECK(cSpawns[0]->FaceTints.Entries[0].Name == tint.Name);

    // The peer casts: the other players replay the action by its graph event names.
    ReferencesMoveRequest movement;
    ActionEvent cast;
    cast.ActionId = 0x13005;
    cast.EventName = "MRh_SpellAimedStart";
    cast.TargetEventName = "BeginCastRight";
    movement.Updates[cPeerPlayer].ActionEvents.push_back(cast);
    session.Receive(kPeer, movement);
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cMoves = session.SentTo<NotifyReferencesMove>(cTo);
        REQUIRE(cMoves.size() == 1);
        const auto& cActions = cMoves[0]->Updates.at(cPeerPlayer).ActionEvents;
        REQUIRE(cActions.size() == 1);
        CHECK(cActions[0].EventName == "MRh_SpellAimedStart");
        CHECK(cActions[0].TargetEventName == "BeginCastRight");
    }
}

TEST_CASE("Mounts: a player getting on the host's horse makes it its game's to move", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    MountRequest mount;
    mount.RiderId = cPeerPlayer;
    mount.MountId = cHorse;
    session.Receive(kPeer, mount);
    // Every game hears it, the rider's to take the horse.
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        REQUIRE(session.SentTo<NotifyMount>(cTo).size() == 1);
        CHECK(session.SentTo<NotifyMount>(cTo)[0]->MountId == cHorse);
    }

    // Its rider's game moves it now; the host's no longer does.
    session.Outbox.clear();
    ReferencesMoveRequest ridden;
    ridden.Updates[cHorse] = {};
    session.Receive(kPeer, ridden);
    REQUIRE(session.SentTo<NotifyReferencesMove>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyReferencesMove>(kOtherPeer)[0]->Updates.contains(cHorse));
    session.Outbox.clear();
    session.Receive(kHost, ridden);
    CHECK(session.SentTo<NotifyReferencesMove>(kOtherPeer).empty());
}

TEST_CASE("Mounts: getting off gives the horse back to the host", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    MountRequest mount;
    mount.RiderId = cPeerPlayer;
    mount.MountId = cHorse;
    session.Receive(kPeer, mount);

    DismountRequest dismount;
    dismount.RiderId = cPeerPlayer;
    dismount.MountId = cHorse;
    session.Receive(kOtherPeer, dismount); // not its rider
    CHECK(session.SentTo<NotifyDismount>(kHost).empty());

    session.Receive(kPeer, dismount);
    // Every game hears it, the host's to take the horse back.
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        REQUIRE(session.SentTo<NotifyDismount>(cTo).size() == 1);
        CHECK(session.SentTo<NotifyDismount>(cTo)[0]->MountId == cHorse);
    }

    // The host's game moves it again; the rider's no longer does.
    session.Outbox.clear();
    ReferencesMoveRequest moved;
    moved.Updates[cHorse] = {};
    session.Receive(kPeer, moved);
    CHECK(session.SentTo<NotifyReferencesMove>(kOtherPeer).empty());
    session.Receive(kHost, moved);
    CHECK(session.SentTo<NotifyReferencesMove>(kOtherPeer).size() == 1);
}

TEST_CASE("Mounts: a rider leaving gives its horse back to the host, which keeps it", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHorse = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);
    MountRequest mount;
    mount.RiderId = cPeerPlayer;
    mount.MountId = cHorse;
    session.Receive(kPeer, mount);
    session.Outbox.clear();

    session.Leave(kPeer);
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cRemoved = session.SentTo<NotifyRemoveCharacter>(cTo);
        CHECK(std::ranges::none_of(cRemoved, [&](const auto* apRemove) { return apRemove->SessionId == cHorse; }));
        REQUIRE(session.SentTo<NotifyDismount>(cTo).size() == 1);
        CHECK(session.SentTo<NotifyDismount>(cTo)[0]->MountId == cHorse);
    }
    // The host's game moves it again.
    session.Outbox.clear();
    ReferencesMoveRequest moved;
    moved.Updates[cHorse] = {};
    session.Receive(kHost, moved);
    CHECK(session.SentTo<NotifyReferencesMove>(kOtherPeer).size() == 1);
}

TEST_CASE("Mounts: the host's NPC on its horse reaches the others; no one mounts a rider not theirs", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cRider = session.Character(kHost);
    const uint32_t cHorse = session.Character(kHost);

    MountRequest mount;
    mount.RiderId = cRider;
    mount.MountId = cHorse;
    session.Receive(kPeer, mount); // not its rider
    CHECK(session.Outbox.empty());

    session.Receive(kHost, mount);
    CHECK(session.SentTo<NotifyMount>(kHost).empty());
    for (const auto cTo : {kPeer, kOtherPeer})
        CHECK(session.SentTo<NotifyMount>(cTo).size() == 1);
}

TEST_CASE("Characters: a player's effects go into its record, for the spawns made later", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    RequestActiveEffects effects;
    effects.SessionId = cPeerPlayer;
    effects.EffectIds = {GameId(0, 0xB94EC)};
    session.Receive(kPeer, effects);
    REQUIRE(session.SentTo<NotifyActiveEffects>(kHost).size() == 1);

    // A player joining later gets it as it is now, not as it was announced.
    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(cLate);
    const auto cPeer = std::ranges::find_if(cSpawns, [&](const auto* apSpawn) { return apSpawn->SessionId == cPeerPlayer; });
    REQUIRE(cPeer != cSpawns.end());
    CHECK((*cPeer)->EffectIds == effects.EffectIds);
}

TEST_CASE("Characters: only a player's owner changes its copies' perk ranks, including later spawns", "[HostAuthority][Characters][perks]")
{
    Session session;
    ActorPerksRequest perks;
    perks.SessionId = session.Character(kPeer);
    perks.Content.Ranks = {{GameId(0, 0xCC799), 2}};
    session.Outbox.clear();
    session.Receive(kOtherPeer, perks);
    CHECK(session.Outbox.empty());
    session.Receive(kPeer, perks);
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cSent = session.SentTo<NotifyActorPerks>(cTo);
        REQUIRE(cSent.size() == 1);
        CHECK(cSent[0]->Content == perks.Content);
    }
    CHECK(session.SentTo<NotifyActorPerks>(kPeer).empty());

    constexpr ConnectionId_t cLate = 4;
    session.Join(cLate, JoinRequest("Serana"));
    AnnounceCharacterRequest late;
    late.SessionId = session.CharacterId(cLate);
    late.ReferenceId = GameId(0, 0x14);
    session.Receive(cLate, late);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(cLate);
    const auto cPeer = std::ranges::find_if(cSpawns, [&](const auto* apSpawn) { return apSpawn->SessionId == perks.SessionId; });
    REQUIRE(cPeer != cSpawns.end());
    CHECK((*cPeer)->PerksContent == perks.Content);
}
