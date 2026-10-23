// HostCharacters: ids, owners, spawns, removal and respawn.
#include "session.h"

#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyPartyTravel.h>

TEST_CASE("Characters: a player's own gets an id, everyone else a spawn, and it everyone else's", "[Bridge][Characters]")
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

    AssignCharacterRequest announce;
    announce.Cookie = 5;
    announce.ReferenceId = GameId(0, 0x14);
    announce.AppearanceBuffer = "face";
    announce.EffectIds = {GameId(0, 0x4B2D9)};
    session.Receive(kPeer, announce);

    const auto cResponses = session.SentTo<AssignCharacterResponse>(kPeer);
    REQUIRE(cResponses.size() == 1);
    CHECK(cResponses[0]->Cookie == 5);
    CHECK(cResponses[0]->Owner);
    const uint32_t cPlayer = cResponses[0]->SessionId;
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
        CHECK(cSpawns[0]->EffectIds == announce.EffectIds);
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

    // The host gave up an announcement it was still waiting on.
    const uint32_t cCancelled = session.Character(kHost);
    CancelAssignmentRequest cancel;
    cancel.Cookie = 3; // the third announcement in this session
    session.Receive(kHost, cancel);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kPeer)[0]->SessionId == cCancelled);

    session.Outbox.clear();
    session.Leave(kPeer);
    REQUIRE(session.SentTo<NotifyRemoveCharacter>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveCharacter>(kHost)[0]->SessionId == cPeerPlayer);
}

TEST_CASE("Characters: the owner's removal says where its actor went, for the copies to follow", "[Bridge][Characters]")
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
    CHECK(cSpawns[0]->Position == movement.Updates[cPeerPlayer].UpdatedMovement.Position);
}

TEST_CASE("Respawn: the fresh spawn starts from the owner's state at its respawn, else the host's copy's", "[Bridge][Characters]")
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

TEST_CASE("Respawn: a peer that lost the host's first character gets it again", "[Bridge][Characters]")
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

TEST_CASE("Arriving: a player gets the host's actors there; players' copies travel with the party", "[Bridge][Characters]")
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

TEST_CASE("Arriving: the host's actors only go to players with the host", "[Bridge][Characters]")
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
    AssignCharacterRequest player;
    player.Cookie = 1;
    player.ReferenceId = GameId(0, 0x14);
    player.CellId = GameId(0, 0x32AE7);
    aSession.Receive(kHost, player);
    // The host's player, as its engine shows it.
    NotifyCharacterSpawn hostPlayer;
    hostPlayer.SessionId = aSession.SentTo<AssignCharacterResponse>(kHost)[0]->SessionId;
    aSession.Spawns.push_back(hostPlayer);
    aSession.Receive(kPeer, player);
    const uint32_t cPeerPlayer = aSession.SentTo<AssignCharacterResponse>(kPeer)[0]->SessionId;
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
    CHECK(session.SentTo<NotifyCharacterSpawn>(kHost).empty());
    CHECK(session.SentTo<NotifyCharacterSpawn>(kPeer).empty());
    // The peer arrives: its copy on the host came along, nothing is spawned for it.
    session.Receive(kPeer, Arrival(0x9732, 0x3C));
    CHECK(session.SentTo<NotifyCharacterSpawn>(kHost).empty());
    (void)cPeerPlayer;
}

TEST_CASE("Arriving: a peer ahead of the host gets the host's actors when the host arrives", "[Bridge][Characters]")
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

TEST_CASE("Party tether: a player's own travel takes everyone else along, the host too", "[Bridge][Characters]")
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

TEST_CASE("Party tether: a follower's arrival takes nobody along", "[Bridge][Characters]")
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

TEST_CASE("Party tether: a follower that arrives where the party is not is sent on", "[Bridge][Characters]")
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

TEST_CASE("Arriving: a fresh spawn of a player is as it arrived, not as it joined", "[Bridge][Characters]")
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

TEST_CASE("Player respawn: everyone else respawns its character", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    session.Receive(kPeer, PlayerRespawnRequest{});
    REQUIRE(session.SentTo<NotifyRespawn>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRespawn>(kHost)[0]->ActorId == cPeerPlayer);
    CHECK(session.SentTo<NotifyRespawn>(kPeer).empty());
}

TEST_CASE("Names: actions and face tints arrive as sent, with nothing shared first", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    // A tinted NPC of the host's, announced as a player joins: its spawn carries the texture names.
    AssignCharacterRequest announce;
    announce.Cookie = 77;
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
