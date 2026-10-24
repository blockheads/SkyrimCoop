#pragma once

// A session for the host's authority (HostAuthority) with no game: three players (the host "Dovahkiin", level 20;
// the peers "Lydia", 12, and "Aela", 8), each an in-process route of the host's network, joined as their games do,
// a fake host game, messages in as the transport sends them, and what goes out captured per connection.
// Characters are announced through it, as the games do.
#include <catch2/catch.hpp>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostNetwork.h>

#include <Messages/Relayed.h>
#include <Messages/ActivateRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/CellStateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/WorldState.h>
#include <Messages/NotifyTimeSettings.h>
#include <Messages/SetTimeCommandRequest.h>
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/NotifyMessageFactory.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/PlayerLevelRequest.h>
#include <BuildVersion.h>

#include <map>
#include <set>
#include <memory>
#include <vector>

namespace HostAuthorityTest
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
    std::unique_ptr<NotifyMessage> Message;
};

struct Session
{
    Session()
        : m_authority(m_network, Game())
    {
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
        m_characterIds[aPlayer] = cResponses.back()->CharacterId;
        return cResponses.back()->PlayerId;
    }
    // The player id a player got when it joined.
    uint32_t PlayerId(ConnectionId_t aPlayer) const { return m_playerIds.at(aPlayer); }
    // The session id its join gave a player's character.
    uint32_t CharacterId(ConnectionId_t aPlayer) const { return m_characterIds.at(aPlayer); }
    // A session id for one of the host's actors, as the host's game takes it.
    uint32_t NewCharacterId() { return m_authority.NewCharacterId(); }

    // A character aOwner announces, as its game does: the host one of its NPCs, a peer its player. Returns its
    // session id once another game got its spawn; what the announcement sent is cleared.
    uint32_t Character(ConnectionId_t aOwner)
    {
        AnnounceCharacterRequest announce;
        announce.SessionId = aOwner == kHost ? NewCharacterId() : CharacterId(aOwner);
        announce.ReferenceId = aOwner == kHost ? GameId(1, 0x1000 + ++m_npcs) : GameId(0, 0x14);
        Receive(aOwner, announce);
        REQUIRE(std::ranges::any_of(m_games, [&](ConnectionId_t aGame) {
            return aGame != aOwner && std::ranges::any_of(SentTo<NotifyCharacterSpawn>(aGame), [&](const auto* apSpawn) {
                       return apSpawn->SessionId == announce.SessionId;
                   });
        }));
        Outbox.clear();
        return announce.SessionId;
    }

    void Leave(ConnectionId_t aPlayer)
    {
        m_games.erase(aPlayer);
        m_network.CloseRoute(aPlayer);
    }
    void Frame(uint64_t aTick) { m_authority.Update(aTick); }
    // The host's player picks a difficulty (its game's DifficultyChangeEvent).
    void ChooseDifficulty(uint32_t aDifficulty)
    {
        Difficulty = aDifficulty;
        m_authority.DifficultyChanged(aDifficulty);
    }

    void Receive(ConnectionId_t aFrom, const RequestMessage& acMessage)
    {
        // A game's first message connects it; the first game is the host's own.
        if (m_games.insert(aFrom).second)
            m_network.OpenRoute(aFrom, [this, aFrom](const void* apData, uint32_t aSize) { Capture(aFrom, apData, aSize); });
        Buffer buffer(1 << 16);
        Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        m_network.Receive(buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()), aFrom);
    }

    template <class T> std::vector<const T*> SentTo(ConnectionId_t aTo) const
    {
        std::vector<const T*> found;
        for (const auto& cSent : Outbox)
            if (cSent.To == aTo && cSent.Message->GetOpcode() == T::Opcode)
                found.push_back(static_cast<const T*>(cSent.Message.get()));
        return found;
    }

    std::vector<NotifyCharacterSpawn> Spawns;                // the host's own actors in its game
    std::unordered_map<uint32_t, Vector3_NetQuantize> Copies; // where the host's copies of players stand
    TimeModel HostClock;                                     // the host's game clock
    std::optional<GameId> Weather;                           // the host's weather to share
    uint32_t GlobalReads = 0;
    std::vector<HostGlobal> GlobalChanges;
    std::vector<HostGlobal> Globals;                              // the host's global variables
    uint32_t QuestReads = 0;
    std::vector<QuestState> QuestChanges;
    std::vector<QuestState> Quests;                               // the host's quests
    std::vector<std::tuple<GameId, bool, uint8_t>> LocksSet; // guests' lock changes the host's game took
    std::vector<std::tuple<GameId, GameId, uint32_t>> ItemsSet; // guests' container changes the host's game took
    std::vector<ObjectChange> ObjectChanges;                      // the host's world objects' changes, not yet taken
    std::map<uint32_t, std::vector<ObjectChange>> LoadedCells;    // the cells loaded in the host's game (by base id), their state
    std::vector<ChangedReference> GuestChanged;                   // what the last cell read was told a guest changed
    uint32_t Difficulty = 3;                                 // the host's difficulty (Expert)
    std::vector<Sent> Outbox;

private:
    void Capture(ConnectionId_t aTo, const void* apData, uint32_t aSize)
    {
        Buffer::Reader reader(std::span(static_cast<const uint8_t*>(apData), aSize));
        if (auto pMessage = NotifyMessageFactory{}.Extract(reader))
            Outbox.push_back({aTo, std::move(pMessage)});
    }

    HostGame Game()
    {
        HostGame game;
        game.OwnSpawns = [this](uint32_t aExcludedSessionId) {
            std::vector<NotifyCharacterSpawn> spawns;
            for (const auto& cSpawn : Spawns)
                if (cSpawn.SessionId != aExcludedSessionId)
                    spawns.push_back(cSpawn);
            return spawns;
        };
        game.ReadTime = [this] { return HostClock; };
        game.SetHour = [this](float aHours) { HostClock.Time = aHours; };
        game.ReadWeather = [this] { return Weather; };
        game.ReadGlobals = [this] { ++GlobalReads; return Globals; };
        game.TakeGlobalChanges = [this] { return std::exchange(GlobalChanges, {}); };
        game.ReadQuests = [this] { ++QuestReads; return Quests; };
        game.TakeQuestChanges = [this] { return std::exchange(QuestChanges, {}); };
        game.TakeObjectChanges = [this] { return std::exchange(ObjectChanges, {}); };
        game.ReadCellState = [this](const GameId& acCell, const std::vector<ChangedReference>& acChanged) -> std::optional<std::vector<ObjectChange>> {
            GuestChanged = acChanged;
            const auto it = LoadedCells.find(acCell.BaseId);
            if (it == LoadedCells.end())
                return std::nullopt;
            return it->second;
        };
        game.SetLock = [this](const GameId& acObject, bool aLocked, uint8_t aLevel) { LocksSet.emplace_back(acObject, aLocked, aLevel); };
        game.SetItems = [this](const GameId& acObject, const GameId& acItem, uint32_t aCount) { ItemsSet.emplace_back(acObject, acItem, aCount); };
        game.ReadDifficulty = [this] { return Difficulty; };
        game.Versions = kHostGame;
        game.Plugins = HostPlugins();
        game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) {
            const auto it = Copies.find(aSessionId);
            if (it == Copies.end())
                return false;
            aSpawn.Position = it->second;
            return true;
        };
        return game;
    }

    uint32_t m_npcs = 0;
    std::map<ConnectionId_t, uint32_t> m_playerIds;
    std::map<ConnectionId_t, uint32_t> m_characterIds;
    std::set<ConnectionId_t> m_games;
    HostNetwork m_network;
    HostAuthority m_authority;
};
} // namespace HostAuthorityTest

using namespace HostAuthorityTest;

