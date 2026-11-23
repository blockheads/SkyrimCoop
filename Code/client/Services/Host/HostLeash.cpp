#include <Services/Host/HostLeash.h>

#include <Messages/NotifyPartyTravel.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostDeath.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>
#include <Structs/GridCellCoords.h>

HostLeash::HostLeash(HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostDeath& acDeath) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(aCharacters)
    , m_death(acDeath)
{
}

void HostLeash::Send(ConnectionId_t aPlayer, NotifyLeash::Kind aState) const noexcept
{
    NotifyLeash notify;
    notify.State = aState;
    notify.Seconds = aState == NotifyLeash::Kind::kOut ? kCountdownSeconds : 0;
    m_network.Send(aPlayer, notify, Net::kReliable);
}

void HostLeash::Update(double aDelta) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    const auto cHostAt = m_characters.OutdoorsAt(cHost);
    for (const ConnectionId_t cPlayer : m_players.Connections())
    {
        if (cPlayer == cHost)
            continue;
        const auto it = m_out.find(cPlayer);
        // Sent to the host: until it arrives nothing counts; a trip that never lands is sent again.
        if (m_characters.OnWayToHost(cPlayer))
        {
            if (it != m_out.end() && it->second.Sent && (it->second.Left -= aDelta) <= 0.0)
            {
                spdlog::warn("HostLeash: {:x} has not arrived at the host in {} s: sent again", cPlayer, kRetrySeconds);
                it->second.Left = kRetrySeconds;
                m_characters.BringToHost(cPlayer);
            }
            continue;
        }
        const auto cAt = m_characters.OutdoorsAt(cPlayer);
        // Both outdoors in one worldspace and standing: indoors and other worldspaces are the party tether's (load
        // doors take everyone), a downed player's place is its respawn's (HostDeath).
        const bool cApplies = cHostAt && cAt && cAt->WorldSpaceId == cHostAt->WorldSpaceId && !m_death.Down(cPlayer) &&
                              !m_death.Down(cHost);
        const bool cOut = cApplies && !GridCellCoords::IsCellInGridCell(GridCellCoords::CalculateGridCellCoords(cAt->Position),
                                                                        GridCellCoords::CalculateGridCellCoords(cHostAt->Position), false);
        if (cOut && it == m_out.end())
        {
            // Nothing said yet: a step over the grid's edge and back is no warning.
            m_out[cPlayer] = {kGraceSeconds, false, false};
        }
        else if (cOut && !it->second.Told)
        {
            if ((it->second.Left -= aDelta) > 0.0)
                continue;
            it->second.Told = true;
            it->second.Left = kCountdownSeconds;
            Send(cPlayer, NotifyLeash::Kind::kOut);
            spdlog::info("HostLeash: {:x} is out of the host's grid: {} s to come back", cPlayer, kCountdownSeconds);
        }
        else if (!cOut && it != m_out.end())
        {
            if (it->second.Told && !it->second.Sent)
            {
                Send(cPlayer, cApplies ? NotifyLeash::Kind::kBack : NotifyLeash::Kind::kOff);
                spdlog::info("HostLeash: {:x}'s countdown stops: {}", cPlayer, cApplies ? "back in the host's grid" : "no leash here");
            }
            m_out.erase(it);
        }
        else if (cOut && (it->second.Left -= aDelta) <= 0.0)
        {
            // Sent already and arrived out of the grid (the host moved on meanwhile): sent again.
            it->second.Sent = it->second.Told = true;
            it->second.Left = kRetrySeconds;
            spdlog::info("HostLeash: {:x} stayed out of the host's grid: sent to the host", cPlayer);
            m_characters.BringToHost(cPlayer);
        }
    }
}

void HostLeash::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_out.erase(aPlayer);
}
