// The host's bridge rules against a headless server: players and their characters are made
// directly in the server's World, messages go in as the transport would send them, and what the
// server sends out is captured per connection.
#include <catch2/catch.hpp>

#include <GameServer.h>
#include <Game/Player.h>
#include <Services/HostBridge.h>

#include <Messages/AddTargetRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/SpellCastRequest.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/ServerMessageFactory.h>

#include <memory>
#include <vector>

namespace
{
constexpr ConnectionId_t kHost = 1;
constexpr ConnectionId_t kPeer = 2;
constexpr ConnectionId_t kOtherPeer = 3;

struct Sent
{
    ConnectionId_t To;
    UniquePtr<ServerMessage> Message;
};

struct Session
{
    Session()
        : m_pServer(std::make_unique<Server::GameServer>(Console(), GameServerTestMode{}))
        , m_bridge(*m_pServer, Game())
    {
        // Sends arrive as [packet type byte][opcode][payload].
        m_pServer->SetTestSendCallback([this](ConnectionId_t aTo, const void* apData, uint32_t aSize) {
            ViewBuffer buffer(static_cast<uint8_t*>(const_cast<void*>(apData)) + 1, aSize - 1);
            Buffer::Reader reader(&buffer);
            if (auto pMessage = ServerMessageFactory{}.Extract(reader))
                Outbox.push_back({aTo, std::move(pMessage)});
        });
        for (const auto cConnection : {kHost, kPeer, kOtherPeer})
            World().GetPlayerManager().Create(cConnection);
        World().GetPlayerManager().SetHostConnection(kHost);
    }

    ~Session() { m_pServer.reset(); } // The server calls into the bridge until it is gone.

    Server::World& World() { return m_pServer->GetWorld(); }

    // A character aOwner announces, as its game does: the host one of its NPCs, a peer its player.
    // Returns its server id; what the announcement sent is cleared.
    uint32_t Character(ConnectionId_t aOwner)
    {
        AssignCharacterRequest announce;
        announce.Cookie = ++m_cookie;
        announce.ReferenceId = aOwner == kHost ? GameId(1, 0x1000 + m_cookie) : GameId(0, 0x14);
        Receive(aOwner, announce);
        const auto cResponses = SentTo<AssignCharacterResponse>(aOwner);
        REQUIRE(cResponses.size() == 1);
        const uint32_t cServerId = cResponses[0]->ServerId;
        Outbox.clear();
        return cServerId;
    }

    void Leave(ConnectionId_t aPlayer) { m_pServer->InjectDisconnection(aPlayer); }

    void Receive(ConnectionId_t aFrom, const ClientMessage& acMessage)
    {
        Buffer buffer(1 << 16);
        Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        m_pServer->InjectMessage(buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()), aFrom);
    }

    template <class T> std::vector<const T*> SentTo(ConnectionId_t aTo) const
    {
        std::vector<const T*> found;
        for (const auto& cSent : Outbox)
            if (cSent.To == aTo && cSent.Message->GetOpcode() == T::Opcode)
                found.push_back(static_cast<const T*>(cSent.Message.get()));
        return found;
    }

    Vector<CharacterSpawnRequest> Spawns;                // the host's own actors in its game
    TiltedPhoques::Map<uint32_t, Vector3_NetQuantize> Copies; // where the host's copies of players stand
    std::vector<Sent> Outbox;

private:
    HostBridge::HostGame Game()
    {
        HostBridge::HostGame game;
        game.OwnSpawns = [this](uint32_t aExcludedServerId) {
            Vector<CharacterSpawnRequest> spawns;
            for (const auto& cSpawn : Spawns)
                if (cSpawn.ServerId != aExcludedServerId)
                    spawns.push_back(cSpawn);
            return spawns;
        };
        game.ReadCopy = [this](uint32_t aServerId, CharacterSpawnRequest& aSpawn) {
            const auto it = Copies.find(aServerId);
            if (it == Copies.end())
                return false;
            aSpawn.Position = it->second;
            return true;
        };
        return game;
    }

    uint32_t m_cookie = 0;
    // One per process: it takes over the static setting and command lists when created.
    static ServerConsole::ConsoleRegistry& Console()
    {
        static ServerConsole::ConsoleRegistry s_console("BridgeTest");
        return s_console;
    }

    std::unique_ptr<Server::GameServer> m_pServer;
    HostBridge m_bridge;
};
} // namespace

TEST_CASE("Hands: the owner's change reaches everyone else", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cActor = session.Character(kPeer);

    DrawWeaponRequest request;
    request.Id = cActor;
    request.IsWeaponDrawn = true;
    session.Receive(kPeer, request);

    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyDrawWeapon>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Id == cActor);
        CHECK(cNotifies[0]->IsWeaponDrawn);
    }
    CHECK(session.SentTo<NotifyDrawWeapon>(kPeer).empty());
}

TEST_CASE("Hands: only the owner decides", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    DrawWeaponRequest request;
    request.Id = cHostNpc;
    request.IsWeaponDrawn = true;
    session.Receive(kPeer, request);

    CHECK(session.SentTo<NotifyDrawWeapon>(kHost).empty());
    CHECK(session.SentTo<NotifyDrawWeapon>(kOtherPeer).empty());
}

TEST_CASE("Equipment snapshot: relayed from the owner only", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestEquipmentSnapshot snapshot;
    snapshot.ServerId = cHostNpc;
    session.Receive(kPeer, snapshot);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kOtherPeer).empty());

    session.Receive(kHost, snapshot);
    REQUIRE(session.SentTo<NotifyEquipmentSnapshot>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kPeer)[0]->ServerId == cHostNpc);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kHost).empty());
}

TEST_CASE("Items: anyone may change an actor's, the owner's game applies it", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestInventoryChanges change;
    change.ServerId = cHostNpc;
    change.Item.BaseId = GameId(0, 0x12EB7);
    change.Item.Count = 1;
    change.Drop = true;
    change.UpdateClients = true;
    session.Receive(kPeer, change);

    const auto cToHost = session.SentTo<NotifyInventoryChanges>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->ServerId == cHostNpc);
    CHECK(cToHost[0]->Item.Count == 1);
    CHECK_FALSE(cToHost[0]->Drop); // dropped items are not synced
    CHECK(session.SentTo<NotifyInventoryChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyInventoryChanges>(kPeer).empty());

    change.UpdateClients = false;
    session.Outbox.clear();
    session.Receive(kPeer, change);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Equipment: a change reaches everyone else", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cActor = session.Character(kPeer);

    RequestEquipmentChanges change;
    change.ServerId = cActor;
    change.ItemId = GameId(0, 0x12EB7);
    change.Count = 1;
    session.Receive(kPeer, change);

    REQUIRE(session.SentTo<NotifyEquipmentChanges>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentChanges>(kHost)[0]->ItemId == GameId(0, 0x12EB7));
    CHECK(session.SentTo<NotifyEquipmentChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentChanges>(kPeer).empty());
}

TEST_CASE("Values: the owner's reach everyone else, nobody else's do", "[Bridge][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestActorValueChanges values;
    values.Id = cHostNpc;
    values.Values[24] = 50.f; // health
    session.Receive(kHost, values);
    REQUIRE(session.SentTo<NotifyActorValueChanges>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorValueChanges>(kPeer)[0]->Values.at(24) == 50.f);
    CHECK(session.SentTo<NotifyActorValueChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorValueChanges>(kHost).empty());

    session.Outbox.clear();
    session.Receive(kPeer, values);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Death: from the owner only", "[Bridge][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestDeathStateChange death;
    death.Id = cHostNpc;
    death.IsDead = true;
    session.Receive(kPeer, death);
    CHECK(session.Outbox.empty());

    session.Receive(kHost, death);
    REQUIRE(session.SentTo<NotifyDeathStateChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyDeathStateChange>(kPeer)[0]->IsDead);
}

TEST_CASE("Hits: go to the actor's owner alone", "[Bridge][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestHealthChangeBroadcast hit;
    hit.Id = cHostNpc;
    hit.DeltaHealth = -12.f;
    session.Receive(kPeer, hit);

    const auto cToHost = session.SentTo<NotifyHealthChangeBroadcast>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->DeltaHealth == -12.f);
    CHECK(session.SentTo<NotifyHealthChangeBroadcast>(kOtherPeer).empty());

    // The owner's own hits are already applied in its game.
    session.Outbox.clear();
    session.Receive(kHost, hit);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Casts and shots: replayed everywhere else, from the caster's owner only", "[Bridge][Magic]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cHostNpc = session.Character(kHost);

    SpellCastRequest cast;
    cast.CasterId = cPeerPlayer;
    cast.SpellFormId = GameId(0, 0x12FCD); // firebolt
    session.Receive(kPeer, cast);
    REQUIRE(session.SentTo<NotifySpellCast>(kHost).size() == 1);
    CHECK(session.SentTo<NotifySpellCast>(kHost)[0]->SpellFormId == GameId(0, 0x12FCD));
    CHECK(session.SentTo<NotifySpellCast>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifySpellCast>(kPeer).empty());

    ProjectileLaunchRequest shot;
    shot.ShooterID = cPeerPlayer;
    shot.OriginX = 10.f;
    session.Receive(kPeer, shot);
    REQUIRE(session.SentTo<NotifyProjectileLaunch>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kHost)[0]->OriginX == 10.f);

    session.Outbox.clear();
    cast.CasterId = cHostNpc;
    session.Receive(kPeer, cast);
    shot.ShooterID = cHostNpc;
    session.Receive(kPeer, shot);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Spell effects: applied by the target's owner alone", "[Bridge][Magic]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cHostNpc = session.Character(kHost);

    AddTargetRequest effect;
    effect.TargetId = cHostNpc;
    effect.CasterId = cPeerPlayer;
    effect.Magnitude = 25.f;
    session.Receive(kPeer, effect);

    REQUIRE(session.SentTo<NotifyAddTarget>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyAddTarget>(kHost)[0]->Magnitude == 25.f);
    CHECK(session.SentTo<NotifyAddTarget>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifyAddTarget>(kPeer).empty());

    // On its own actor the caster's game already applied it.
    session.Outbox.clear();
    effect.TargetId = cPeerPlayer;
    session.Receive(kPeer, effect);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Spell removal: reaches everyone else", "[Bridge][Magic]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RemoveSpellRequest removal;
    removal.TargetId = cHostNpc;
    removal.SpellId = GameId(0, 0x12FCD);
    session.Receive(kPeer, removal);

    REQUIRE(session.SentTo<NotifyRemoveSpell>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveSpell>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyRemoveSpell>(kPeer).empty());
}

TEST_CASE("Factions: each actor's from its owner only", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    RequestFactionsChanges changes;
    changes.Changes[cPeerPlayer] = {};
    changes.Changes[cHostNpc] = {}; // not the peer's to say
    session.Receive(kPeer, changes);

    const auto cToHost = session.SentTo<NotifyFactionsChanges>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->Changes.size() == 1);
    CHECK(cToHost[0]->Changes.count(cPeerPlayer) == 1);
    CHECK(session.SentTo<NotifyFactionsChanges>(kPeer).empty());

    session.Outbox.clear();
    changes.Changes.erase(cPeerPlayer);
    session.Receive(kPeer, changes);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Mounts, subtitles, experience: reach everyone else", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cHorse = session.Character(kHost);

    MountRequest mount;
    mount.RiderId = cHostNpc;
    mount.MountId = cHorse;
    session.Receive(kPeer, mount);
    REQUIRE(session.SentTo<NotifyMount>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyMount>(kHost)[0]->MountId == cHorse);

    SubtitleRequest subtitle;
    subtitle.ServerId = cHostNpc;
    subtitle.Text = "Let me guess...";
    subtitle.TopicFormId = 0x1234;
    session.Receive(kHost, subtitle);
    const auto cSubtitles = session.SentTo<NotifySubtitle>(kPeer);
    REQUIRE(cSubtitles.size() == 1);
    CHECK(cSubtitles[0]->Text == "Let me guess...");
    CHECK(cSubtitles[0]->TopicFormId == 0x1234);

    SyncExperienceRequest experience;
    experience.Experience = 12.5f;
    session.Receive(kPeer, experience);
    REQUIRE(session.SentTo<NotifySyncExperience>(kHost).size() == 1);
    CHECK(session.SentTo<NotifySyncExperience>(kOtherPeer)[0]->Experience == 12.5f);
    CHECK(session.SentTo<NotifySyncExperience>(kPeer).empty());
}

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
