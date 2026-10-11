// The host's bridge rules against a headless server: players and their characters are made
// directly in the server's World, messages go in as the transport would send them, and what the
// server sends out is captured per connection.
#include <catch2/catch.hpp>

#include <GameServer.h>
#include <Game/Player.h>
#include <Services/HostBridge.h>

#include <Messages/AddTargetRequest.h>
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
        : m_console("BridgeTest")
        , m_pServer(std::make_unique<Server::GameServer>(m_console, GameServerTestMode{}))
        , m_bridge(*m_pServer, [this](uint32_t aJoinerServerId) {
            Vector<CharacterSpawnRequest> spawns;
            for (const auto& cSpawn : Spawns)
                if (cSpawn.ServerId != aJoinerServerId)
                    spawns.push_back(cSpawn);
            return spawns;
        })
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

    // A character owned by aOwner; returns its server id.
    uint32_t Character(ConnectionId_t aOwner)
    {
        const auto cEntity = World().create();
        World().emplace<Server::OwnerComponent>(cEntity, World().GetPlayerManager().GetByConnectionId(aOwner));
        return Server::World::ToInteger(cEntity);
    }

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

    Vector<CharacterSpawnRequest> Spawns; // what the host's game shows
    std::vector<Sent> Outbox;

private:
    ServerConsole::ConsoleRegistry m_console;
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

TEST_CASE("Late join: every character the host knows but the joiner's own", "[Bridge][Characters]")
{
    Session session;
    for (const uint32_t cServerId : {10u, 11u, 12u})
    {
        CharacterSpawnRequest spawn;
        spawn.ServerId = cServerId;
        session.Spawns.push_back(spawn);
    }

    const auto& cLateJoin = Server::GameServer::Get()->GetLateJoinHandler();
    REQUIRE(cLateJoin);
    cLateJoin(kPeer, 11);

    const auto cSpawns = session.SentTo<CharacterSpawnRequest>(kPeer);
    REQUIRE(cSpawns.size() == 2);
    CHECK(cSpawns[0]->ServerId == 10);
    CHECK(cSpawns[1]->ServerId == 12);
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
