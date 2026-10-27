#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostCrime.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

HostCrime::HostCrime(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& aPlayers,
                     const HostCharacters& aCharacters, const HostGame& aGame) noexcept
    : m_network(aNetwork), m_players(aPlayers), m_characters(aCharacters), m_game(aGame)
{
    aAuthority.On(*this, &HostCrime::OnRequest);
}

void HostCrime::OnRequest(const CrimeRequest& aRequest, ConnectionId_t aSender) noexcept
{
    if (!m_players.IsPlayer(aSender) || aSender == m_players.Host() || !m_game.JudgeCrime || !aRequest.IsValid() ||
        aRequest.Sequence <= m_sequences[aSender])
        return;
    const auto actor = m_characters.PlayerCharacterOf(aSender);
    if (!actor)
        return;
    m_sequences[aSender] = aRequest.Sequence;
    const bool lock = aRequest.Action == CrimeAction::Lockpick;
    // The native menu can check repeatedly as the pick turns. Once caught, this attempt is
    // already charged; another check only repeats its answer. Leaving discards the attempt.
    const bool caught = (lock && m_caughtLocks[aSender] == aRequest.Count) || m_game.JudgeCrime(actor, aRequest);
    if (aRequest.Action == CrimeAction::Lockpick && caught)
    {
        m_caughtLocks[aSender] = aRequest.Count;
        NotifyLockCrime result;
        result.Attempt = aRequest.Count;
        m_network.Send(aSender, result);
    }
    Update();
}

void HostCrime::Update() noexcept
{
    if (m_game.TakeCrimeConfiscations)
        for (const auto& due : m_game.TakeCrimeConfiscations())
        {
            const auto owner = m_characters.OwnerOf(due.Actor);
            if (!owner || owner == m_players.Host())
                continue;
            NotifyCrimeConfiscation message;
            message.Id = ++m_confiscationId;
            message.Faction = due.Faction;
            m_network.Send(owner, message);
        }
    if (!m_game.ReadCrimeGold)
        return;
    auto gold = m_game.ReadCrimeGold();
    if (gold == m_gold)
        return;
    m_gold = std::move(gold);
    NotifyCrimeState state;
    state.Gold = m_gold;
    m_players.SendToOthers(state, m_players.Host());
}

void HostCrime::Entered(ConnectionId_t aPlayer) noexcept
{
    if (aPlayer == m_players.Host() || !m_game.ReadCrimeGold)
        return;
    NotifyCrimeState state;
    state.Gold = m_game.ReadCrimeGold();
    m_network.Send(aPlayer, state);
}
