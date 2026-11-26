#pragma once

// A session for the host's authority (HostAuthority) with no game: three players (the host "Dovahkiin", level 20;
// the peers "Lydia", 12, and "Aela", 8), each an in-process route of the host's network, joined as their games do,
// a fake host game, messages in as the transport sends them, and what goes out captured per connection.
// Characters are announced through it, as the games do.
#include <catch2/catch.hpp>

#include <game/HostAuthority.h>
#include <Messages/ContainerTransferRequest.h>
#include <core/HostNetwork.h>
#include <core/RecipeBook.h>

#include <Messages/Relayed.h>
#include <Messages/ActivateRequest.h>
#include <Messages/SummonRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/CellStateRequest.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/WeatherRegionRequest.h>
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
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RelayedMessages.h>
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
#include <Messages/NotifyStealthEye.h>
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
// A rider's get-on: a claim on its horse (HostCharacters' ride rule).
inline ClaimRequest RideClaim(uint32_t aRider, uint32_t aMount)
{
    ClaimRequest claim;
    claim.Kind = Payloads::Claim::kRide;
    claim.ForId = aRider;
    claim.Members = {{aMount, true, false}};
    return claim;
}

// A kill move: its attacker locked, its victim taken (an NPC, its death kept) or locked (a player).
inline ClaimRequest KillMoveClaim(uint32_t aAttacker, uint32_t aVictim, bool aVictimIsPlayer = false, uint64_t aTick = 0)
{
    ClaimRequest claim;
    claim.Kind = Payloads::Claim::kKillMove;
    claim.ForId = aAttacker;
    claim.Tick = aTick;
    claim.Members = {{aAttacker, false, false}, {aVictim, !aVictimIsPlayer, !aVictimIsPlayer}};
    return claim;
}

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
        m_network.SetRecipes(&Recipes);
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
    HostAuthority& Authority() { return m_authority; }
    // The host's game took back an actor given back (its copy ran the holder's ops: HostService).
    void Taken(uint32_t aActorId) { m_authority.ActorTaken(aActorId); }

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
    // The host's game shows a character change location (its engine's TESActorLocationChangeEvent).
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
    std::vector<CrimeReport> CrimeReports; // the host engine's bounty lines (37679), taken each frame
    uint32_t PartyGold{}; // the host's player's gold, the purse
    // The host's player's party stacks (party-items.md), by base and name.
    std::map<std::pair<GameId, std::string>, int32_t> PartyItems;
    std::vector<std::pair<uint32_t, CrimeRequest>> Crimes;
    std::function<void(uint32_t, const CrimeRequest&)> CrimeJudge;
    // What the host's game moved for a guest's container transfer (HostGame::TransferContainer); none: what was asked.
    std::function<Inventory::Entry(const ContainerTransferRequest&)> Transferred;
    // Items' form types for the host's game (any other: a weapon, 41).
    std::unordered_map<GameId, uint8_t> FormTypes;

    std::vector<NotifyCharacterSpawn> Spawns;                // the host's own actors in its game
    std::unordered_map<uint32_t, Vector3_NetQuantize> Copies; // where the host's copies of players stand
    TimeModel HostClock;                                     // the host's game clock
    std::optional<GameId> Weather;                           // the host's weather to share
    bool WeatherForced = false;                              // forced by a script or the console (Sky's override)
    GameId WeatherRegion;                                    // the host's player's weather region
    std::map<uint32_t, GameId> RegionWeathers;               // each region's own weather in the host's game, by base id
    std::map<uint32_t, DetectionRules::EyeReading> Eyes;      // the host's NPCs' reading of a character's copy, by id
    std::vector<std::string> ConsoleLines;                   // the dev commands' lines the host's console ran
    std::set<uint32_t> CannotPair;                           // the host's NPCs dead or with their graph stopped
    std::set<uint32_t> Dead;                                 // the host's actors dead (HostGame::IsDead)
    std::map<uint32_t, std::string> ConsoleNames;            // a character as the host's console names it
    uint32_t HostilesKilled = 0;                             // /killall's kills in the host's game
    std::vector<std::pair<uint32_t, uint32_t>> PartyHorses;  // /spawn horse: (session id, count) placed
    uint32_t GlobalReads = 0;
    std::vector<HostGlobal> GlobalChanges;
    std::vector<HostGlobal> Globals;                              // the host's global variables
    uint32_t QuestReads = 0;
    std::vector<QuestState> QuestChanges;
    std::vector<QuestState> Quests;                               // the host's quests
    std::vector<std::tuple<GameId, uint32_t, uint32_t>> Activations; // guests' activations the host's game ran
    std::vector<std::pair<uint32_t, GameId>> Summons; // guests' summons the host's game made: caster, spell
    std::vector<Payloads::TrapContact> TrapsTouched;
    std::vector<std::tuple<GameId, bool, uint8_t>> LocksSet; // guests' lock changes the host's game took
    std::vector<StoryEvent> StoriesRaised;                    // guests' story events the host's story manager queued
    std::vector<ObjectChange> ItemsSet;                      // guests' container changes the host's game took
    std::unordered_map<GameId, Inventory::Entry> Loose;              // the host's loose items it made for guests' drops
    std::set<GameId> QuestObjects;                                    // those a quest's alias holds
    std::vector<GameId> TakenForParty;                                // quest objects the host's player picked up
    uint32_t LooseMade = 0;
    uint32_t CreateCalls = 0;
    std::vector<ObjectChange> ObjectChanges;                      // the host's world objects' changes, not yet taken
    std::map<uint32_t, ObjectChange> CreatedContents;             // the created containers' contents, by created base id
    std::map<uint32_t, std::vector<ObjectChange>> LoadedCells;    // the cells loaded in the host's game (by base id), their state
    std::vector<ChangedReference> GuestChanged;                   // what the last cell read was told a guest changed
    std::vector<BodyKey> Ended;     // a guest's authority ended: the host's game simulates it again (HostGame::EndPlayback)
    std::vector<BodyKey> Touches;   // the host's player's pushes and hits this update (HostGame::TakeTouches)
    std::vector<std::pair<BodyKey, BodyKey>> Contacts; // pairs Havok made (HostGame::ReadTouching)
    std::vector<BodyPose> Bodies;   // the host's bodies where they are
    std::vector<std::pair<GameId, glm::vec3>> Placed;     // references the host's game itself moved (a script, the console)
    HostBodyReads Moving;           // the host's physics this update, taken by the next frame
    std::map<uint32_t, glm::vec3> PlayerPositions; // where the host's game shows each guest's character, by player id
    uint32_t Difficulty = 3;                                 // the host's difficulty (Expert)
    std::optional<std::vector<Followers::State>> Followers;
    std::vector<Followers::Order> FollowerOrders;
    Followers::Anchors FollowerBindings;
    std::vector<Followers::Target> FollowerTargets;
    std::vector<Sent> Outbox;
    // The host's game's recipes (HostService gives its World's).
    RecipeBook Recipes;
    // The host's saves: the one its game makes for a joining game (none: not made yet), the images it still has, and
    // how many bytes each encodes to.
    std::optional<std::string> WorldSave;
    std::vector<std::string> SaveImages;
    size_t SaveBytes = 100;

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
        game.SaveWorld = [this] { return WorldSave; };
        // As SaveLibrary::Encode: against the first held image it still has, else whole.
        game.EncodeSave = [this](const std::string& acSave, const std::vector<std::string>& acHeld) {
            auto pStream = std::make_shared<SaveTransfer::Stream>();
            if (std::ranges::find(SaveImages, acSave) == SaveImages.end())
                return pStream;
            pStream->Save = acSave;
            for (const auto& cHeld : acHeld)
                if (pStream->Base.empty() && cHeld != acSave && std::ranges::find(SaveImages, cHeld) != SaveImages.end())
                    pStream->Base = cHeld;
            pStream->Size = 1000;
            pStream->Data.assign(SaveBytes, 'd');
            return pStream;
        };
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
        game.TakeCrimeReports = [this] { return std::exchange(CrimeReports, {}); };
        game.ReadPartyGold = [this] { return PartyGold; };
        game.AddPartyGold = [this](int32_t aCount) { PartyGold = static_cast<uint32_t>(std::max<int64_t>(int64_t{PartyGold} + aCount, 0)); };
        game.ReadPartyItem = [this](const Inventory::Entry& acItem) { return PartyItems[{acItem.BaseId, acItem.ExtraName}]; };
        game.AddPartyItem = [this](const Inventory::Entry& acItem) {
            auto& count = PartyItems[{acItem.BaseId, acItem.ExtraName}];
            count = std::max(count + acItem.Count, 0);
        };
        game.ReadPartyItems = [this] {
            std::vector<Inventory::Entry> items;
            for (const auto& [cStack, cCount] : PartyItems)
                if (cCount > 0)
                {
                    Inventory::Entry& item = items.emplace_back();
                    item.BaseId = cStack.first;
                    item.ExtraName = cStack.second;
                    item.Count = cCount;
                }
            return items;
        };
        game.ItemName = [](const GameId&) { return std::string("Iron Sword"); };
        game.ItemFormType = [this](const GameId& acItem) { const auto it = FormTypes.find(acItem); return it != FormTypes.end() ? it->second : uint8_t(41); };
        // A host chest gives or takes what was asked, unless a test says what moved.
        game.TransferContainer = [this](const ContainerTransferRequest& acRequest, uint32_t) {
            return Transferred ? Transferred(acRequest) : acRequest.Item;
        };
        game.JudgeCrime = [this](uint32_t aActor, const CrimeRequest& aCrime) {
            Crimes.emplace_back(aActor, aCrime);
            if (CrimeJudge) CrimeJudge(aActor, aCrime);
            return CrimeCaught;
        };
        game.CanPair = [this](uint32_t aSessionId) { return !CannotPair.contains(aSessionId); };
        game.IsDead = [this](uint32_t aSessionId) { return Dead.contains(aSessionId); };
        game.ReadTime = [this] { return HostClock; };
        game.SetHour = [this](float aHours) { HostClock.Time = aHours; };
        game.AdvanceTime = [this](float aHours) { HostClock.Time += aHours; };
        game.ReadWeather = [this]() -> std::optional<HostSky> {
            if (!Weather)
                return std::nullopt;
            return HostSky{.Weather = *Weather, .Forced = WeatherForced, .Region = WeatherRegion};
        };
        game.RegionWeather = [this](const GameId& acRegion) -> std::optional<GameId> {
            const auto it = RegionWeathers.find(acRegion.BaseId);
            return it == RegionWeathers.end() ? std::nullopt : std::optional(it->second);
        };
        game.ReadEye = [this](uint32_t aSessionId) -> std::optional<DetectionRules::EyeReading> {
            const auto it = Eyes.find(aSessionId);
            return it == Eyes.end() ? std::nullopt : std::optional(it->second);
        };
        game.RunConsole = [this](const std::string& acLine) { ConsoleLines.push_back(acLine); };
        game.ConsoleNameOf = [this](uint32_t aSessionId) -> std::optional<std::string> {
            const auto it = ConsoleNames.find(aSessionId);
            return it == ConsoleNames.end() ? std::nullopt : std::optional(it->second);
        };
        game.KillHostiles = [this] { return ++HostilesKilled; };
        game.SpawnPartyHorses = [this](uint32_t aSessionId, uint32_t aCount) {
            PartyHorses.emplace_back(aSessionId, aCount);
            return aCount;
        };
        game.ReadGlobals = [this] { ++GlobalReads; return Globals; };
        game.TakeGlobalChanges = [this] { return std::exchange(GlobalChanges, {}); };
        game.ReadQuests = [this] { ++QuestReads; return Quests; };
        game.TakeQuestChanges = [this] { return std::exchange(QuestChanges, {}); };
        game.TakeObjectChanges = [this] { return std::exchange(ObjectChanges, {}); };
        game.TakePlaced = [this] { return std::exchange(Placed, {}); };
        game.ReadCellState = [this](const GameId& acCell, const std::vector<ChangedReference>& acChanged) -> std::optional<std::vector<ObjectChange>> {
            GuestChanged = acChanged;
            const auto it = LoadedCells.find(acCell.BaseId);
            if (it == LoadedCells.end())
                return std::nullopt;
            return it->second;
        };
        game.ReadContents = [this](const GameId& acObject) -> std::optional<ObjectChange> {
            const auto it = acObject.ModId == ObjectChange::kCreatedModId ? CreatedContents.find(acObject.BaseId) : CreatedContents.end();
            return it != CreatedContents.end() ? std::optional(it->second) : std::nullopt;
        };
        game.Summon = [this](uint32_t aCasterId, const SummonRequest& acSummon) { Summons.emplace_back(aCasterId, acSummon.SpellId); };
        game.Activate = [this](const GameId& acObject, uint32_t aNpcId, uint32_t aActivatorId) { Activations.emplace_back(acObject, aNpcId, aActivatorId); };
        game.TouchTrap = [this](const Payloads::TrapContact& acContact) { TrapsTouched.push_back(acContact); };
        game.SetLock = [this](const GameId& acObject, bool aLocked, uint8_t aLevel) { LocksSet.emplace_back(acObject, aLocked, aLevel); };
        game.SetItems = [this](const ObjectChange& acItems) { ItemsSet.push_back(acItems); };
        game.RaiseStoryEvent = [this](const StoryEvent& acEvent) { StoriesRaised.push_back(acEvent); };
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
        game.TakeQuestObject = [this](const GameId& acObject) {
            if (!QuestObjects.erase(acObject) || !Loose.erase(acObject))
                return false;
            TakenForParty.push_back(acObject);
            return true;
        };
        game.TakeItem = [this](const GameId& acObject) -> std::optional<Inventory::Entry> {
            const auto it = Loose.find(acObject);
            if (it == Loose.end())
                return std::nullopt;
            auto item = it->second;
            Loose.erase(it);
            return item;
        };
        game.EndPlayback = [this](const BodyKey& acKey, uint64_t) { Ended.push_back(acKey); };
        game.TakeTouches = [this] { return std::exchange(Touches, {}); };
        game.ReadTouching = [this](const std::vector<BodyKey>& acKeys) {
            std::vector<std::pair<BodyKey, BodyKey>> pairs;
            for (const auto& cPair : Contacts)
                if (std::ranges::find(acKeys, cPair.first) != acKeys.end())
                    pairs.push_back(cPair);
            return pairs;
        };
        game.TakeBodies = [this](uint64_t) { return std::exchange(Moving, {}); };
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
        game.CopySpawn = [](uint32_t aSessionId) {
            NotifyCharacterSpawn spawn;
            spawn.SessionId = aSessionId;
            return std::optional{spawn};
        };
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

