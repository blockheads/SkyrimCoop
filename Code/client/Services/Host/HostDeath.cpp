#include <Services/Host/HostDeath.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>

#include <algorithm>
#include <cmath>

HostDeath::HostDeath(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
                     const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostDeath::OnDeath);
    aAuthority.Watch(*this, &HostDeath::OnAiState);
}

void HostDeath::Entered(ConnectionId_t aPlayer, uint32_t aCharacterId) noexcept
{
    m_party[aPlayer].Body.SessionId = aCharacterId;
    Reconsider();
    Broadcast();
}

void HostDeath::InitialAi(uint32_t aCharacterId, const ActorAiState& acState) noexcept
{
    m_ai[aCharacterId] = acState;
    Reconsider();
}

void HostDeath::Left(ConnectionId_t aPlayer) noexcept
{
    if (const auto it = m_party.find(aPlayer); it != m_party.end())
    {
        m_ai.erase(it->second.Body.SessionId);
        m_party.erase(it);
    }
    Reconsider();
    Broadcast();
}

void HostDeath::OnAiState(const AiStateRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (m_characters.OwnerOf(acRequest.SessionId) != aSender)
        return;
    m_ai[acRequest.SessionId] = acRequest.State;
    if (!acRequest.State.Combat)
        m_targets.erase(acRequest.SessionId);
    else if (acRequest.State.CombatTarget.SessionId)
        m_targets[acRequest.SessionId].insert(acRequest.State.CombatTarget.SessionId);
    Reconsider();
}

void HostDeath::OnDeath(const PlayerDeathRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const auto it = m_party.find(aSender);
    if (m_wiped || it == m_party.end() || acRequest.SessionId != m_characters.PlayerCharacterOf(aSender))
        return;
    Player& player = it->second;
    // An older fall's message comes late.
    if (acRequest.Generation < player.Body.Generation)
        return;
    if (acRequest.Down)
    {
        // Each fall once (a repeat of this one changes nothing).
        if (!acRequest.Generation || (acRequest.Generation == player.Body.Generation && player.Known))
            return;
        player.Body = {acRequest.SessionId, acRequest.Generation, true};
        NotifyPlayerPerished notice;
        notice.PlayerId = m_players.PlayerIdOf(aSender);
        notice.Username = m_players.PlayerNameOf(aSender);
        m_players.SendToAll(notice);
    }
    else
    {
        // Up again only when the host respawned it for this fall (or it was never down).
        if (player.Body.Down && (!player.Respawning || acRequest.Generation != player.Body.Generation))
            return;
        player.Body = {acRequest.SessionId, acRequest.Generation, false};
    }
    player.Known = true;
    player.Respawning = false;
    Reconsider();
    Broadcast();
}

void HostDeath::Reconsider() noexcept
{
    if (m_wiped)
        return;
    size_t down = 0;
    std::set<uint32_t> standing;
    bool known = !m_party.empty();
    for (const auto& [cConnection, cPlayer] : m_party)
    {
        known &= cPlayer.Known;
        if (cPlayer.Body.Down)
            ++down;
        else
            standing.insert(cPlayer.Body.SessionId);
    }
    if (known && down && standing.empty())
    {
        m_wiped = true;
        m_recoveryAt.reset();
        Broadcast();
        if (m_game.PartyWipe)
            m_game.PartyWipe();
        return;
    }

    const bool cCombat = StandingPlayerFights(standing);
    const bool cCombatChanged = cCombat != m_combat;
    m_combat = cCombat;
    // The timer runs while someone is down, everyone's state is known and no standing player fights; a fight starts
    // it over.
    const auto cBefore = m_recoveryAt;
    if (!known || !down || cCombat)
        m_recoveryAt.reset();
    else if (!m_recoveryAt)
        m_recoveryAt = m_now + kRecoverySeconds;
    if (cBefore != m_recoveryAt || cCombatChanged)
        Broadcast();
}

bool HostDeath::StandingPlayerFights(const std::set<uint32_t>& acStanding) const noexcept
{
    for (const auto& [cId, cAi] : m_ai)
    {
        if (!cAi.Combat)
            continue;
        // A standing player's character fighting (a downed one's stale flag holds nothing).
        if (acStanding.contains(cId))
            return true;
        // A host NPC fighting a standing player, now or since its fight started (it switches targets).
        if (m_characters.OwnerOf(cId) != m_players.Host())
            continue;
        if (acStanding.contains(cAi.CombatTarget.SessionId))
            return true;
        if (const auto it = m_targets.find(cId); it != m_targets.end())
            if (std::ranges::any_of(it->second, [&](uint32_t aTarget) { return acStanding.contains(aTarget); }))
                return true;
    }
    return false;
}

void HostDeath::Update(double aDelta) noexcept
{
    m_now += std::max(0.0, aDelta);
    if (m_recoveryAt && m_now >= *m_recoveryAt)
        Respawn();
}

void HostDeath::Respawn() noexcept
{
    m_recoveryAt.reset();
    // Beside the standing player with the lowest session id the host can place.
    std::optional<NotifyPartyTravel> anchor;
    uint32_t anchorId = 0;
    for (const auto& [cConnection, cPlayer] : m_party)
        if (cPlayer.Known && !cPlayer.Body.Down && (!anchorId || cPlayer.Body.SessionId < anchorId))
            if (auto at = m_characters.CharacterPlace(cPlayer.Body.SessionId))
            {
                anchor = std::move(at);
                anchorId = cPlayer.Body.SessionId;
            }
    if (!anchor)
    {
        Reconsider();
        return;
    }
    size_t slot = 0;
    for (auto& [cConnection, player] : m_party)
    {
        if (!player.Body.Down || player.Respawning)
            continue;
        NotifyPlayerRespawn respawn;
        respawn.SessionId = player.Body.SessionId;
        respawn.Generation = player.Body.Generation;
        respawn.AnchorId = anchorId;
        respawn.CellId = anchor->CellId;
        respawn.WorldSpaceId = anchor->WorldSpaceId;
        respawn.Position = anchor->Position;
        const float cAngle = static_cast<float>(slot++) * kRespawnTurn;
        respawn.Position.x += kRespawnDistance * std::cos(cAngle);
        respawn.Position.y += kRespawnDistance * std::sin(cAngle);
        respawn.Heading = anchor->Heading;
        player.Respawning = true;
        m_network.Send(cConnection, respawn);
    }
}

void HostDeath::Broadcast() const noexcept
{
    NotifyPartyDeath party;
    for (const auto& [cConnection, cPlayer] : m_party)
        party.Players.push_back(cPlayer.Body);
    party.Combat = m_combat;
    party.Wiped = m_wiped;
    party.Error = m_error;
    if (m_recoveryAt)
        party.RemainingMs = static_cast<uint32_t>(std::ceil(std::max(0.0, *m_recoveryAt - m_now) * 1000.0));
    m_players.SendToAll(party);
}

void HostDeath::FailReload(const std::string& acError) noexcept
{
    m_error = acError;
    Broadcast();
}
