#pragma once

// A session for the host's authority (HostAuthority) against a headless server: three players (the host "Dovahkiin",
// level 20, on the in-process route; the peers "Lydia", 12, and "Aela", 8), joined as their games do, a fake host game, messages in as the transport sends them, and what goes out captured
// per connection. Characters are announced through it, as the games do.
#include <catch2/catch.hpp>

#include <GameServer.h>
#include <Game/Player.h>
#include <Services/Host/HostAuthority.h>

#include <Messages/AddTargetRequest.h>
#include <Messages/ActivateRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/WorldState.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/SetTimeCommandRequest.h>
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
#include <Messages/NotifyActiveEffects.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestActiveEffects.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/PlayerLevelRequest.h>
#include <BuildVersion.h>

#include <map>
#include <memory>
#include <vector>

namespace BridgeTest
{
constexpr ConnectionId_t kHost = 1;
constexpr ConnectionId_t kPeer = 2;
constexpr ConnectionId_t kOtherPeer = 3;
// The host's Skyrim 1.6.1170 with SKSE 2.2.6.
constexpr GameVersions kHostGame{GameVersions::Pack(1, 6, 1170), GameVersions::Pack(2, 2, 6)};

// The host's plugins: Skyrim.esm, Update.esm, a light ccBGSSSE001-Fish.esm.
inline Mods HostPlugins()
{
    Mods plugins;
    plugins.ModList.push_back({"Skyrim.esm", 0});
    plugins.ModList.push_back({"Update.esm", 1});
    plugins.ModList.push_back({"ccBGSSSE001-Fish.esm", 0, true});
    return plugins;
}

// What a game on the host's build, game and plugins sends to join.
inline AuthenticationRequest JoinRequest(const char* acpName, uint16_t aLevel = 5)
{
    AuthenticationRequest join;
    join.Version = BuildCommit();
    join.Game = kHostGame;
    join.UserMods = HostPlugins();
    join.Username = acpName;
    join.Level = aLevel;
    return join;
}

struct Sent
{
    ConnectionId_t To;
    UniquePtr<ServerMessage> Message;
};

struct Session
{
    Session()
        : m_pServer(std::make_unique<Server::GameServer>(Console(), GameServerTestMode{}))
        , m_authority(*m_pServer, Game())
    {
        m_pServer->SetTestSendCallback([this](ConnectionId_t aTo, const void* apData, uint32_t aSize) { Capture(aTo, apData, aSize); });
        // The host's game is the one in the server's process.
        m_pServer->SetLoopbackRoute(kHost, [this](const void* apData, uint32_t aSize) { Capture(kHost, apData, aSize); });
        Join(kHost, JoinRequest("Dovahkiin", 20));
        Join(kPeer, JoinRequest("Lydia", 12));
        Join(kOtherPeer, JoinRequest("Aela", 8));
        Outbox.clear();
    }

    // aPlayer joins as its game does; returns its player id.
    uint32_t Join(ConnectionId_t aPlayer, const AuthenticationRequest& acRequest)
    {
        Receive(aPlayer, acRequest);
        const auto cResponses = SentTo<AuthenticationResponse>(aPlayer);
        REQUIRE(!cResponses.empty());
        m_playerIds[aPlayer] = cResponses.back()->PlayerId;
        return cResponses.back()->PlayerId;
    }
    // The player id a player got when it joined.
    uint32_t PlayerId(ConnectionId_t aPlayer) const { return m_playerIds.at(aPlayer); }

    ~Session() { m_pServer.reset(); } // The server calls into HostAuthority until it is gone.

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
    void Frame(uint64_t aTick) { m_authority.Update(aTick); }

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
    TimeModel HostClock;                                     // the host's game clock
    std::optional<GameId> Weather;                           // the host's weather to share
    Vector<HostGlobal> Globals;                              // the host's global variables
    Vector<QuestState> Quests;                               // the host's quests
    std::vector<std::tuple<GameId, bool, uint8_t>> LocksSet; // guests' lock changes the host's game took
    Vector<ObjectChange> ObjectChanges;                      // the host's world objects' changes, not yet taken
    uint32_t Difficulty = 3;                                 // the host's difficulty (Expert)
    std::vector<Sent> Outbox;

private:
    // Sends arrive as [packet type byte][opcode][payload].
    void Capture(ConnectionId_t aTo, const void* apData, uint32_t aSize)
    {
        ViewBuffer buffer(static_cast<uint8_t*>(const_cast<void*>(apData)) + 1, aSize - 1);
        Buffer::Reader reader(&buffer);
        if (auto pMessage = ServerMessageFactory{}.Extract(reader))
            Outbox.push_back({aTo, std::move(pMessage)});
    }

    HostGame Game()
    {
        HostGame game;
        game.OwnSpawns = [this](uint32_t aExcludedServerId) {
            Vector<CharacterSpawnRequest> spawns;
            for (const auto& cSpawn : Spawns)
                if (cSpawn.ServerId != aExcludedServerId)
                    spawns.push_back(cSpawn);
            return spawns;
        };
        game.ReadTime = [this] { return HostClock; };
        game.SetHour = [this](float aHours) { HostClock.Time = aHours; };
        game.ReadWeather = [this] { return Weather; };
        game.ReadGlobals = [this] { return Globals; };
        game.ReadQuests = [this] { return Quests; };
        game.TakeObjectChanges = [this] { return std::exchange(ObjectChanges, {}); };
        game.SetLock = [this](const GameId& acObject, bool aLocked, uint8_t aLevel) { LocksSet.emplace_back(acObject, aLocked, aLevel); };
        game.ReadDifficulty = [this] { return Difficulty; };
        game.Versions = kHostGame;
        game.Plugins = HostPlugins();
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
    std::map<ConnectionId_t, uint32_t> m_playerIds;
    // One per process: it takes over the static setting and command lists when created.
    static ServerConsole::ConsoleRegistry& Console()
    {
        static ServerConsole::ConsoleRegistry s_console("BridgeTest");
        return s_console;
    }

    std::unique_ptr<Server::GameServer> m_pServer;
    HostAuthority m_authority;
};
} // namespace BridgeTest

using namespace BridgeTest;

