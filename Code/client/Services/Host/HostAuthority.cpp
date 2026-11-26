// No game or client World here: tests build it with in-process games (Code/tests/host_authority).
#include <Services/Host/HostAuthority.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostValues.h>

#include <Diagnostics/Profile.h>

HostAuthority::HostAuthority(HostNetwork& aNetwork, HostGame aGame) noexcept
    : m_network(aNetwork)
    , m_game(std::move(aGame))
    , m_players(aNetwork)
    , m_router(aNetwork, m_players)
    , m_chat(m_router, aNetwork, m_players)
    , m_transfer(aNetwork, m_game)
    , m_session(m_router, aNetwork, m_players, m_characters, m_transfer, m_game)
    , m_characters(m_router, aNetwork, m_players, m_game)
    , m_clock(m_router, aNetwork, m_players, m_game)
    , m_wait(m_router, aNetwork, m_players, m_game, m_game)
    , m_crime(m_router, aNetwork, m_players, m_characters, m_game)
    , m_partyItems(m_players, m_characters, m_game)
    , m_weather(m_router, aNetwork, m_players, m_game)
    , m_eye(m_players, m_characters, m_game)
    , m_worldState(m_router, aNetwork, m_players, m_game)
    , m_items(m_router, aNetwork, m_players, m_characters, m_partyItems, m_game)
    , m_bodies(m_router, aNetwork, m_players, m_characters, m_game)
    , m_timeScale(m_router, aNetwork, m_players)
    , m_dialogue(m_router, aNetwork, m_players, m_characters)
    , m_playerNatives(m_router, aNetwork, m_players, m_characters)
    , m_records(m_router, m_players, m_characters, m_game)
    , m_story(m_router, m_players, m_characters, m_game)
    , m_death(m_router, aNetwork, m_players, m_characters)
    , m_leash(aNetwork, m_players, m_characters, m_death)
    , m_followers(m_router, aNetwork, m_players, m_characters, m_death, m_game)
    , m_devTools(m_router, aNetwork, m_players, m_characters, m_game, m_leash)
    , m_objects(m_router, m_players, m_characters, m_game)
    , m_combat(m_router, m_players, m_characters, m_game)
    , m_magic(m_router, m_players, m_characters, m_game)
    , m_graph(m_router, m_players, m_characters)
    , m_map(m_router, aNetwork)
{
    using enum Delivery;
    m_router.SetOwners([this](uint32_t aActor) { return m_characters.OwnerOf(aActor); });
    m_characters.SetListeners({
        .PlayerBodyEntered = [this](ConnectionId_t aPlayer, uint32_t aId) { PlayerBodyEntered(aPlayer, aId); },
        .PlayerEntered = [this](ConnectionId_t aPlayer, uint32_t aId) { PlayerEntered(aPlayer, aId); },
        .ActorAnnounced = [this](uint32_t aId, const RecordParts& acRecord) { ActorAnnounced(aId, acRecord); },
        .ActorRemoved = [this](uint32_t aId) { ActorRemoved(aId); },
        .RecordOf = [this](uint32_t aId) { return RecordOf(aId); },
    });
    m_records.OnKept = [this](uint32_t aId, const RecordPart& acPart) {
        ActorAiState state;
        if (acPart.Kind == RecordKind::kAiState && FromPartBytes(acPart.Bytes, state))
            m_death.OnAiState(aId, state);
        // A player's character's name is that player's in the party's lists.
        if (acPart.Kind == RecordKind::kName && m_characters.IsPlayerCharacter(aId))
            m_players.Rename(m_characters.OwnerOf(aId), acPart.Bytes);
    };

    // Rules of parts built before the router could take them.
    m_players.Listen(m_router);
    m_router.On(m_transfer, &HostSaveTransfer::OnSaveHeld);
    RelayActorValues(m_router);

    // Everything a joiner must know, from every store that holds it.
    m_joinerStores = {
        {"crime", [this](ConnectionId_t aPlayer, uint32_t) { m_crime.Entered(aPlayer); }},
        {"clock", [this](ConnectionId_t aPlayer, uint32_t) { m_clock.SendTime(aPlayer); }},
        {"weather", [this](ConnectionId_t aPlayer, uint32_t) { m_weather.SendWeather(aPlayer); }},
        {"world state", [this](ConnectionId_t aPlayer, uint32_t) { m_worldState.SendWorldState(aPlayer); }},
        {"time scale", [this](ConnectionId_t aPlayer, uint32_t) { m_timeScale.SendTimeScale(aPlayer); }},
        // Every character with its record, and the claims on them.
        {"characters", [this](ConnectionId_t aPlayer, uint32_t aId) { m_characters.SendExistingCharacters(aPlayer, aId); }},
        // Its player and every copy carry the party's gold and items, its own brought in replaced.
        {"party items", [this](ConnectionId_t, uint32_t) { m_partyItems.Send(); }},
        {"cutscene controls", [this](ConnectionId_t aPlayer, uint32_t) { m_playerNatives.SendHeld(aPlayer); }},
        {"waypoint", [this](ConnectionId_t aPlayer, uint32_t) { m_map.SendWaypoint(aPlayer); }},
        {"conversation", [this](ConnectionId_t aPlayer, uint32_t) { m_dialogue.SendCurrent(aPlayer); }},
    };

    m_network.SetFrontDoor([this](const RequestMessage& acMessage, ConnectionId_t aSender) { return Handle(acMessage, aSender); });
    // A player's game ended its connection: the others drop it, then its characters go.
    m_network.SetDisconnectionHandler([this](ConnectionId_t aConnection) {
        m_transfer.OnDisconnected(aConnection);
        if (!m_players.IsPlayer(aConnection))
            return;
        m_wait.OnPlayerLeft(aConnection);
        m_crime.Left(aConnection);
        m_session.OnPlayerLeft(aConnection);
        m_playerNatives.OnPlayerLeft(aConnection);
        m_records.OnPlayerLeft(aConnection);
        m_characters.OnPlayerLeft(aConnection);
        m_leash.OnPlayerLeft(aConnection);
        m_players.Remove(aConnection);
        m_timeScale.OnPlayerLeft(aConnection);
        m_dialogue.OnPlayerLeft(aConnection);
        m_worldState.OnPlayerLeft(aConnection);
        m_items.OnPlayerLeft(aConnection);
        m_bodies.OnPlayerLeft(aConnection);
        m_death.Left(aConnection);
        m_eye.OnPlayerLeft(aConnection);
        m_weather.OnPlayerLeft(aConnection);
    });
}

void HostAuthority::Update(uint64_t aTick, double aDelta, uint64_t aFrameTick) noexcept
{
    PROFILE_ZONE("HostAuthority::Update");
    // Only the save transfer runs until the host's game loads it.
    if (m_reloading)
    {
        m_transfer.Update(aTick);
        return;
    }
    m_death.Update(aDelta);
    m_followers.Update(aTick);
    m_wait.Update(aTick);
    m_crime.Update();
    m_clock.Update(aTick);
    m_leash.Update(aDelta);
    m_transfer.Update(aTick);
    m_weather.Update();
    m_eye.Update();
    m_worldState.Update(aTick);
    m_items.Update();
    if (m_game.TakePlaced)
        for (const auto& [cObject, cWhere] : m_game.TakePlaced())
            m_bodies.OnHostPlaced(cObject, cWhere);
    m_bodies.Update(aFrameTick ? aFrameTick : aTick);
}

void HostAuthority::DifficultyChanged(uint32_t aDifficulty) const noexcept
{
    m_session.OnDifficultyChange(aDifficulty);
}

void HostAuthority::Kick(uint32_t aPlayerId) noexcept
{
    m_players.Kick(aPlayerId);
}

void HostAuthority::ActorAnnounced(uint32_t aId, const RecordParts& acRecord) noexcept
{
    m_records.Announced(aId, acRecord);
    ActorAiState state;
    if (const RecordPart* cpAi = acRecord.Find(RecordKind::kAiState); cpAi && FromPartBytes(cpAi->Bytes, state))
        m_death.InitialAi(aId, state);
}

void HostAuthority::PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterSessionId) noexcept
{
    m_wait.OnPlayerEntered(); // a joiner ends a shared wait's countdown
    for (const JoinerStore& cStore : m_joinerStores)
        cStore.Send(aPlayer, aCharacterSessionId);
}

bool HostAuthority::Handle(const RequestMessage& acMessage, ConnectionId_t aSender) noexcept
{
    const auto cOpcode = acMessage.GetOpcode();
    // What a game holds is heard from any game a save was meant for, a joining one too, and during a reload.
    const bool cHeld = cOpcode == kSaveHeldRequest && (m_players.IsPlayer(aSender) || m_transfer.IsRecipient(aSender));
    if (m_reloading && !cHeld)
        return false;
    // A game that is not a player may only ask to join; anything else from it is dropped.
    if (cOpcode != kAuthenticationRequest && !cHeld && !m_players.IsPlayer(aSender))
        return false;

    return m_router.Dispatch(acMessage, aSender);
}

void HostAuthority::NameActor(uint32_t aSessionId, const std::string& acName) const noexcept
{
    m_characters.NameActor(aSessionId, acName);
}

std::vector<HostAuthority::ConnectionId_t> HostAuthority::Guests() const noexcept
{
    std::vector<ConnectionId_t> guests;
    m_players.ForEach([&](ConnectionId_t aConnection, const HostPlayers::Player& acPlayer) {
        if (!acPlayer.IsHost)
            guests.push_back(aConnection);
    });
    return guests;
}

void HostAuthority::SendCheckpoint(const std::string& acSave) noexcept
{
    m_reloading = true;
    m_transfer.Checkpoint(acSave, Guests());
}

void HostAuthority::OfferSave(const std::string& acSave) noexcept
{
    if (!m_reloading && !m_death.Wiped())
        m_transfer.Offer(acSave, Guests());
}
