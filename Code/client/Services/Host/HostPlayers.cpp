#include <Services/Host/HostPlayers.h>

#include <Services/Host/HostNetwork.h>

HostPlayers::HostPlayers(HostNetwork& aNetwork) noexcept
    : m_network(aNetwork)
{
}

uint32_t HostPlayers::Add(ConnectionId_t aConnection, const std::string& acName, uint16_t aLevel, bool aIsHost, std::string aKey) noexcept
{
    const uint32_t cId = ++m_lastId;
    m_players[aConnection] = {cId, acName, aLevel, aIsHost, std::move(aKey)};
    return cId;
}

void HostPlayers::Remove(ConnectionId_t aConnection) noexcept
{
    m_players.erase(aConnection);
}

const HostPlayers::Player* HostPlayers::Find(ConnectionId_t aConnection) const noexcept
{
    const auto it = m_players.find(aConnection);
    return it == m_players.end() ? nullptr : &it->second;
}

uint32_t HostPlayers::PlayerIdOf(ConnectionId_t aConnection) const noexcept
{
    const auto* cpPlayer = Find(aConnection);
    return cpPlayer ? cpPlayer->Id : 0;
}

std::string HostPlayers::PlayerNameOf(ConnectionId_t aConnection) const noexcept
{
    const auto* cpPlayer = Find(aConnection);
    return cpPlayer ? cpPlayer->Name : std::string{};
}

HostPlayers::ConnectionId_t HostPlayers::Host() const noexcept
{
    for (const auto& [cConnection, cPlayer] : m_players)
        if (cPlayer.IsHost)
            return cConnection;
    return 0;
}

std::vector<HostPlayers::ConnectionId_t> HostPlayers::Connections() const noexcept
{
    std::vector<ConnectionId_t> connections;
    for (const auto& [cConnection, cPlayer] : m_players)
        connections.push_back(cConnection);
    return connections;
}

void HostPlayers::SetLevel(ConnectionId_t aConnection, uint16_t aLevel) noexcept
{
    if (const auto it = m_players.find(aConnection); it != m_players.end())
        it->second.Level = aLevel;
}

void HostPlayers::SendToOthers(const NotifyMessage& acMessage, ConnectionId_t aExcluded, Net::EPacketFlags aFlags) const noexcept
{
    for (const auto& [cConnection, cPlayer] : m_players)
        if (cConnection != aExcluded)
            m_network.Send(cConnection, acMessage, aFlags);
}

void HostPlayers::Kick(uint32_t aPlayerId) noexcept
{
    for (const auto& [cConnection, cPlayer] : m_players)
    {
        if (cPlayer.Id != aPlayerId)
            continue;
        if (cPlayer.IsHost)
            return;
        spdlog::info("HostPlayers: kicking '{}' [{:x}]", cPlayer.Name.c_str(), cConnection);
        m_network.Kick(cConnection);
        return;
    }
}
