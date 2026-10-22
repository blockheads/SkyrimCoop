#include <Services/Host/HostTimeScale.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyTimeScale.h>
#include <Messages/TimeScaleRequest.h>

#include <algorithm>

HostTimeScale::HostTimeScale(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
{
    aAuthority.On(*this, &HostTimeScale::OnTimeScale);
}

void HostTimeScale::OnTimeScale(const TimeScaleRequest& acRequest, ConnectionId_t aSender) noexcept
{
    m_wanted[aSender] = acRequest.Multiplier;
    Tell();
}

void HostTimeScale::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_wanted.erase(aPlayer);
    m_told.erase(aPlayer);
    Tell();
}

void HostTimeScale::SendTimeScale(ConnectionId_t aPlayer) noexcept
{
    if (const float cSlowest = SlowestBesides(aPlayer); cSlowest != 1.f)
        Tell(aPlayer, cSlowest);
}

float HostTimeScale::SlowestBesides(ConnectionId_t aPlayer) const noexcept
{
    float slowest = 1.f;
    for (const auto& [cConnection, cWanted] : m_wanted)
        if (cConnection != aPlayer)
            slowest = std::min(slowest, cWanted);
    return slowest;
}

void HostTimeScale::Tell() noexcept
{
    for (const ConnectionId_t cPlayer : m_players.Connections())
    {
        const float cSlowest = SlowestBesides(cPlayer);
        const auto cTold = m_told.find(cPlayer);
        if ((cTold == m_told.end() ? 1.f : cTold->second) != cSlowest)
            Tell(cPlayer, cSlowest);
    }
}

void HostTimeScale::Tell(ConnectionId_t aPlayer, float aMultiplier) noexcept
{
    m_told[aPlayer] = aMultiplier;
    NotifyTimeScale notify;
    notify.Multiplier = aMultiplier;
    m_network.Send(aPlayer, notify);
}
