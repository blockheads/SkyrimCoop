#include <characters/HostCharacters.h>

#include <core/HostNetwork.h>
#include <characters/HostCharactersGame.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>

#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/Relayed.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>


namespace
{
// For OwnSpawns: exclude no one (ids start at 1).
constexpr uint32_t cNoCharacter = 0;
} // namespace

HostCharacters::HostCharacters(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharactersGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
    , m_claims(aRouter, acPlayers, *this, acGame)
    , m_rides(aRouter, aNetwork, acPlayers, *this, m_claims)
    , m_arrivals(aRouter, aNetwork, acPlayers, *this)
{
    aRouter.On(*this, &HostCharacters::OnAnnounceCharacter);
    aRouter.On(*this, &HostCharacters::OnRemoveCharacter);
    aRouter.On(*this, &HostCharacters::OnRespawn);
    aRouter.On(*this, &HostCharacters::OnActiveEffects);
}

void HostCharacters::OnAnnounceCharacter(const AnnounceCharacterRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const bool cIsPlayer = acRequest.ReferenceId == GameId{0, 0x14};
    if (!cIsPlayer && aSender != m_players.Host())
    {
        // Under the party tether every actor but a player's own is in the host's game.
        spdlog::warn("HostCharacters: {:x} announced {:x}:{:x}, which only the host may", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId);
        return;
    }

    const uint32_t cSessionId = acRequest.SessionId;
    if (cIsPlayer)
    {
        const auto cReserved = m_playerCharacters.find(aSender);
        if (cReserved == m_playerCharacters.end() || cReserved->second != cSessionId)
        {
            spdlog::warn("HostCharacters: {:x} announced its player as {:x}, not the id its join gave it", aSender, cSessionId);
            return;
        }
        // A player has one character: announced again (after a respawn), its copies are made again.
        if (m_characters.contains(cSessionId))
            RemoveCharacter(cSessionId);
    }
    else if (cSessionId == 0 || cSessionId > m_lastSessionId || m_characters.contains(cSessionId) || IsPlayerCharacter(cSessionId))
    {
        spdlog::warn("HostCharacters: the host's game announced {:x}:{:x} as {:x}, an id it was not given", acRequest.ReferenceId.ModId,
                     acRequest.ReferenceId.BaseId, cSessionId);
        return;
    }

    const uint32_t cPlayerId = cIsPlayer ? m_players.PlayerIdOf(aSender) : 0;
    auto cSpawn = NotifyCharacterSpawn::FromAnnouncement(acRequest, cSessionId, cPlayerId);
    // A seat the host did not give it: its copies stay standing.
    std::erase_if(cSpawn.Record.Parts, [&](const RecordPart& acPart) { return !m_rides.RideGranted(cSessionId, acPart); });

    auto& character = m_characters[cSessionId];
    character.Owner = aSender;
    if (cIsPlayer)
    {
        m_arrivals.Announced(aSender, acRequest.CellId, acRequest.WorldSpaceId);
        character.PlayerRecord = cSpawn;
        m_listeners.PlayerBodyEntered(aSender, cSessionId);
        if (aSender != m_players.Host())
        {
            m_listeners.PlayerEntered(aSender, cSessionId);
            m_arrivals.Joined(aSender, acRequest.Position);
        }
    }

    m_listeners.ActorAnnounced(cSessionId, cSpawn.Record);
    SendToOthersWithHost(cSpawn, aSender);
    spdlog::info("HostCharacters: {:x} announced {:x}:{:x} as {:x}", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId, cSessionId);
}

void HostCharacters::OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept
{
    if (OwnerOf(acRequest.SessionId) == aSender)
        RemoveCharacter(acRequest.SessionId, acRequest.Whereabouts);
}

void HostCharacters::OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept
{
    auto it = m_characters.find(acRequest.ActorId);
    if (it == m_characters.end())
        return;

    if (it->second.Owner != aSender)
    {
        if (const auto cSpawn = BuildSpawn(acRequest.ActorId))
            m_network.Send(aSender, *cSpawn);
        return;
    }

    if (auto& record = it->second.PlayerRecord)
    {
        // The respawned copies start from the record: the owner's state now, else the host's copy's while it exists.
        if (acRequest.HasState)
        {
            record->LatestAction = acRequest.LatestAction;
            record->GraphState = acRequest.GraphState;
            record->InitialActorValues = acRequest.Data.InitialActorValues;
            record->IsDead = acRequest.Data.IsDead;
        }
        else if (acRequest.AppearanceBuffer.empty())
            m_game.ReadCopy(acRequest.ActorId, *record);
    }
    if (!acRequest.AppearanceBuffer.empty())
    {
        auto& record = it->second.PlayerRecord;
        if (!record)
            return;
        record->AppearanceBuffer = acRequest.AppearanceBuffer;
        record->ChangeFlags = acRequest.ChangeFlags;
        record->FaceTints = acRequest.FaceTints;
        record->Weight = acRequest.Weight;
        NotifyRespawn notify;
        notify.ActorId = acRequest.ActorId;
        notify.AppearanceBuffer = acRequest.AppearanceBuffer;
        notify.ChangeFlags = acRequest.ChangeFlags;
        notify.FaceTints = acRequest.FaceTints;
        notify.Weight = acRequest.Weight;
        m_players.SendToOthers(notify, aSender, Net::kReliable);
        return;
    }
    spdlog::info("HostCharacters: {:x} respawns its {}", aSender, acRequest.ActorId);
    NotifyRespawn notify;
    notify.ActorId = acRequest.ActorId;
    m_players.SendToOthers(notify, aSender, Net::kReliable);
}

void HostCharacters::OnActiveEffects(const RequestActiveEffects& acRequest, ConnectionId_t aSender) noexcept
{
    const auto it = m_characters.find(acRequest.SessionId);
    if (it == m_characters.end() || it->second.Owner != aSender)
        return;
    if (auto& record = it->second.PlayerRecord)
    {
        record->EffectIds = acRequest.EffectIds;
        record->HeldEffects = acRequest.Held;
    }
    else
        it->second.Effects = acRequest; // a guest's horse: its owner's word, not the host's copy's list
    NotifyActiveEffects notify;
    static_cast<Payloads::ActiveEffects&>(notify) = acRequest;
    m_players.SendToOthers(notify, aSender);
}

bool HostCharacters::SetOwner(uint32_t aSessionId, ConnectionId_t aTo) noexcept
{
    const auto cCharacter = m_characters.find(aSessionId);
    return cCharacter != m_characters.end() && std::exchange(cCharacter->second.Owner, aTo) != aTo;
}

void HostCharacters::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    const uint32_t cRider = PlayerCharacterOf(aPlayer);
    m_playerCharacters.erase(aPlayer);
    m_arrivals.PlayerLeft(aPlayer);
    std::vector<uint32_t> owned;
    for (const auto& [cSessionId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer && cCharacter.PlayerRecord)
            owned.push_back(cSessionId);
    m_rides.PlayerLeft(aPlayer, cRider);
    // Anything else it still holds (a kill move's partner) is the host's again.
    m_claims.OnPlayerLeft(aPlayer);
    for (const uint32_t cSessionId : owned)
        RemoveCharacter(cSessionId);
}

uint32_t HostCharacters::PlayerCharacterOf(ConnectionId_t aPlayer) const noexcept
{
    for (const auto& [cSessionId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer && cCharacter.PlayerRecord)
            return cSessionId;
    return cNoCharacter;
}

uint32_t HostCharacters::NewCharacterId() noexcept
{
    return ++m_lastSessionId;
}

uint32_t HostCharacters::ReservePlayerCharacter(ConnectionId_t aPlayer) noexcept
{
    return m_playerCharacters[aPlayer] = NewCharacterId();
}

bool HostCharacters::IsPlayerCharacter(uint32_t aSessionId) const noexcept
{
    return std::ranges::any_of(m_playerCharacters, [aSessionId](const auto& acEntry) { return acEntry.second == aSessionId; });
}

void HostCharacters::SendPlayersMet(ConnectionId_t aPlayer) noexcept
{
    if (const auto cSpawn = BuildSpawn(PlayerCharacterOf(aPlayer)))
        SendToOthersWithHost(*cSpawn, aPlayer);
    size_t count = 0;
    for (const auto& [cSessionId, cCharacter] : m_characters)
        if ((cCharacter.PlayerRecord || cCharacter.Seats.Rider) && cCharacter.Owner != aPlayer && m_arrivals.WithHost(cCharacter.Owner))
            if (const auto cSpawn = BuildSpawn(cSessionId))
            {
                m_network.Send(aPlayer, *cSpawn);
                ++count;
            }
    spdlog::info("HostCharacters: {:x} reached the party it was brought to: {} players' characters each way", aPlayer,
                 count);
    m_claims.SendClaims(aPlayer);
    m_rides.SendRides(aPlayer);
}

void HostCharacters::SendHostActors(ConnectionId_t aPlayer) noexcept
{
    const size_t cCount = SendHostSpawns(aPlayer, PlayerCharacterOf(m_players.Host()));
    spdlog::info("HostCharacters: sent {:x} the host's {} actors where it arrived", aPlayer, cCount);
}

size_t HostCharacters::SendHostSpawns(ConnectionId_t aPlayer, uint32_t aExcludedSessionId) noexcept
{
    auto spawns = m_game.OwnSpawns(aExcludedSessionId);
    for (auto& spawn : spawns)
    {
        spawn.Record = m_listeners.RecordOf(spawn.SessionId);
        m_network.Send(aPlayer, spawn);
    }
    return spawns.size();
}

void HostCharacters::SendToOthersWithHost(const NotifyCharacterSpawn& acSpawn, ConnectionId_t aSender) const noexcept
{
    for (const ConnectionId_t cPlayer : m_players.Connections())
        if (cPlayer != aSender && m_arrivals.WithHost(cPlayer))
            m_network.Send(cPlayer, acSpawn, Net::kReliable);
}

HostCharacters::ConnectionId_t HostCharacters::OwnerOf(uint32_t aSessionId) const noexcept
{
    const auto it = m_characters.find(aSessionId);
    return it != m_characters.end() ? it->second.Owner : 0;
}

NotifyCharacterSpawn* HostCharacters::PlayerRecordOf(uint32_t aSessionId, ConnectionId_t aOwner) noexcept
{
    const auto it = m_characters.find(aSessionId);
    if (it == m_characters.end() || it->second.Owner != aOwner || !it->second.PlayerRecord)
        return nullptr;
    return &*it->second.PlayerRecord;
}

RideSeats* HostCharacters::SeatsOf(uint32_t aSessionId) noexcept
{
    const auto it = m_characters.find(aSessionId);
    return it != m_characters.end() ? &it->second.Seats : nullptr;
}

void HostCharacters::ForEachCharacter(const Visit& acVisit) noexcept
{
    for (auto& [cSessionId, character] : m_characters)
        acVisit(cSessionId, character.Owner, character.PlayerRecord ? &*character.PlayerRecord : nullptr, character.Seats);
}

std::optional<NotifyCharacterSpawn> HostCharacters::BuildSpawn(uint32_t aSessionId) const noexcept
{
    auto spawn = ReadSpawn(aSessionId);
    // Its record as its owner last declared it: a spawn read before a change never brings an older part.
    if (spawn)
        spawn->Record = m_listeners.RecordOf(aSessionId);
    return spawn;
}

std::optional<NotifyCharacterSpawn> HostCharacters::ReadSpawn(uint32_t aSessionId) const noexcept
{
    const auto it = m_characters.find(aSessionId);
    if (it == m_characters.end())
        return std::nullopt;

    if (it->second.PlayerRecord && it->second.Owner != m_players.Host())
    {
        auto spawn = *it->second.PlayerRecord;
        m_game.ReadCopy(aSessionId, spawn); // else as recorded
        return spawn;
    }
    // A horse a guest rides, or one on its way back to the host's game: the host's copy of it.
    if (!it->second.PlayerRecord && (it->second.Owner != m_players.Host() || m_claims.Returning(aSessionId)))
    {
        auto spawn = m_game.CopySpawn ? m_game.CopySpawn(aSessionId) : std::nullopt;
        if (spawn && it->second.Effects)
        {
            spawn->EffectIds = it->second.Effects->EffectIds;
            spawn->HeldEffects = it->second.Effects->Held;
        }
        return spawn;
    }

    for (auto& spawn : m_game.OwnSpawns(cNoCharacter))
        if (spawn.SessionId == aSessionId)
            return std::move(spawn);
    return std::nullopt;
}

void HostCharacters::SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnSessionId) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    size_t count = SendHostSpawns(aPlayer, aOwnSessionId);
    for (const auto& [cSessionId, cCharacter] : m_characters)
    {
        // A claimed actor, or one given back the host's game has not taken yet: its own spawns lack them.
        const bool cClaimed = m_claims.HolderOf(cSessionId) || m_claims.Returning(cSessionId);
        if (cSessionId == aOwnSessionId || (cCharacter.Owner == cHost && !m_claims.Returning(cSessionId)) ||
            !(cCharacter.PlayerRecord || cClaimed) || !m_arrivals.WithHost(cCharacter.Owner))
            continue;
        if (const auto cSpawn = BuildSpawn(cSessionId))
        {
            m_network.Send(aPlayer, *cSpawn);
            ++count;
        }
    }
    spdlog::info("HostCharacters: {} characters for the player who joined ({:x})", count, aPlayer);
    m_claims.SendClaims(aPlayer);
    m_rides.SendRides(aPlayer);
}

void HostCharacters::RemoveCharacter(uint32_t aSessionId, const ActorWhereabouts& acWhereabouts) noexcept
{
    const auto it = m_characters.find(aSessionId);
    if (it == m_characters.end())
        return;

    NotifyRemoveCharacter notify;
    notify.SessionId = aSessionId;
    notify.Whereabouts = acWhereabouts;
    m_players.SendToOthers(notify, it->second.Owner, Net::kReliable);

    m_characters.erase(it);
    m_claims.Forget(aSessionId);
    m_listeners.ActorRemoved(aSessionId);
    m_rides.Removed(aSessionId);
}

void HostCharacters::NameActor(uint32_t aSessionId, const std::string& acName) const noexcept
{
    const ConnectionId_t cOwner = OwnerOf(aSessionId);
    if (cOwner == m_players.Host())
        return;
    NotifyActorName notify;
    notify.ActorId = aSessionId;
    notify.Name = acName;
    m_network.Send(cOwner, notify);
}
