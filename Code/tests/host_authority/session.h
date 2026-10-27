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
#include <Messages/TravelMessages.h>
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RelayedMessages.h>
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
    explicit Session(bool aFollowerKeys = false)
        : m_authority(m_network, Game())
    {
        const auto join = [&](ConnectionId_t aConnection, const char* acpName, uint16_t aLevel, const char* acpKey) {
            auto request = JoinRequest(acpName, aLevel);
            if (aFollowerKeys)
                request.PlayerKey = acpKey;
            Join(aConnection, request);
        };
        join(kHost, "Dovahkiin", 20, "host");
        join(kPeer, "Lydia", 12, "guest");
        join(kOtherPeer, "Aela", 8, "other");
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
    void Frame(uint64_t aTick)
    {
        m_authority.Update(aTick, static_cast<double>(aTick - std::exchange(m_lastTick, aTick)) / 1000.0);
    }
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

    bool CrimeCaught{};
    std::vector<CrimeGold> Bounty;
    uint32_t PartyGold{}; // the host's player's gold, the purse
    std::vector<std::pair<uint32_t, CrimeRequest>> Crimes;
    std::function<void(uint32_t, const CrimeRequest&)> CrimeJudge;

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
    std::vector<ObjectChange> ItemsSet;                      // guests' container changes the host's game took
    std::unordered_map<GameId, Inventory::Entry> Loose;              // the host's loose items it made for guests' drops
    uint32_t LooseMade = 0;
    uint32_t CreateCalls = 0;
    std::vector<ObjectChange> ObjectChanges;                      // the host's world objects' changes, not yet taken
    std::map<uint32_t, std::vector<ObjectChange>> LoadedCells;    // the cells loaded in the host's game (by base id), their state
    std::vector<ChangedReference> GuestChanged;                   // what the last cell read was told a guest changed
    std::vector<BodyPush> Pushed;   // guests' pushes the host's game took
    std::vector<BodyFrame> Freed;   // and let go
    std::vector<BodyPose> Bodies;   // the host's bodies where they are
    HostBodies Moving;              // the host's physics this update, taken by the next frame
    std::map<uint32_t, glm::vec3> PlayerPositions; // where the host's game shows each guest's character, by player id
    uint32_t Difficulty = 3;                                 // the host's difficulty (Expert)
    std::optional<std::vector<Followers::State>> Followers;
    std::vector<Followers::Order> FollowerOrders;
    Followers::Anchors FollowerBindings;
    std::vector<Followers::Target> FollowerTargets;
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
        game.ReadFollowers = [this] { return Followers; };
        game.TakeFollowerOrders = [this] { return std::exchange(FollowerOrders, {}); };
        game.FollowerAnchors = [this]() -> Followers::Anchors& { return FollowerBindings; };
        game.SetFollowerTargets = [this](const auto& acTargets) { FollowerTargets = acTargets; };
        game.OwnSpawns = [this](uint32_t aExcludedSessionId) {
            std::vector<NotifyCharacterSpawn> spawns;
            for (const auto& cSpawn : Spawns)
                if (cSpawn.SessionId != aExcludedSessionId)
                    spawns.push_back(cSpawn);
            return spawns;
        };
        game.ReadCrimeGold = [this] { return Bounty; };
        game.ReadPartyGold = [this] { return PartyGold; };
        game.AddPartyGold = [this](int32_t aCount) { PartyGold = static_cast<uint32_t>(std::max<int64_t>(int64_t{PartyGold} + aCount, 0)); };
        game.JudgeCrime = [this](uint32_t aActor, const CrimeRequest& aCrime) {
            Crimes.emplace_back(aActor, aCrime);
            if (CrimeJudge) CrimeJudge(aActor, aCrime);
            return CrimeCaught;
        };
        game.ReadTime = [this] { return HostClock; };
        game.SetHour = [this](float aHours) { HostClock.Time = aHours; };
        game.AdvanceTime = [this](float aHours) { HostClock.Time += aHours; };
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
        game.SetItems = [this](const ObjectChange& acItems) { ItemsSet.push_back(acItems); };
        // A drop is made in a cell the host's game has loaded.
        game.CreateItem = [this](const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) -> std::optional<GameId> {
            ++CreateCalls;
            if (!LoadedCells.contains(acPlacement.Cell.BaseId))
                return std::nullopt;
            if (acItem.BaseId == GameId{})
                return GameId{}; // not in this game
            const GameId cId(ObjectChange::kCreatedModId, 0x800 + ++LooseMade);
            Loose[cId] = acItem;
            return cId;
        };
        game.TakeItem = [this](const GameId& acObject) -> std::optional<Inventory::Entry> {
            const auto it = Loose.find(acObject);
            if (it == Loose.end())
                return std::nullopt;
            auto item = it->second;
            Loose.erase(it);
            return item;
        };
        game.PushBody = [this](const BodyPush& acPush) { Pushed.push_back(acPush); return true; };
        game.FreeBody = [this](const BodyFrame& acFrame) { Freed.push_back(acFrame); return true; };
        game.TakeBodies = [this] { return std::exchange(Moving, {}); };
        game.ReadPlayerPosition = [this](uint32_t aPlayerId) -> std::optional<glm::vec3> {
            const auto it = PlayerPositions.find(aPlayerId);
            return it != PlayerPositions.end() ? std::optional(it->second) : std::nullopt;
        };
        game.ReadBodies = [this](const std::vector<BodyKey>& acKeys) {
            std::vector<BodyPose> poses;
            for (const auto& cBody : Bodies)
                if (std::ranges::find(acKeys, cBody.Key) != acKeys.end())
                    poses.push_back(cBody);
            return poses;
        };
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
    uint64_t m_lastTick = 0;
    std::map<ConnectionId_t, uint32_t> m_playerIds;
    std::map<ConnectionId_t, uint32_t> m_characterIds;
    std::set<ConnectionId_t> m_games;
    HostNetwork m_network;
    HostAuthority m_authority;
};
} // namespace HostAuthorityTest

using namespace HostAuthorityTest;

