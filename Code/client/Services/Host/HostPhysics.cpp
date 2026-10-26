#include <Services/Host/HostPhysics.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>
#include <Physics/BodyRelevance.h>
#include <Services/WorldStateLog.h>

namespace
{
// Frames per packet: about 1 KB, under a datagram's MTU (an unreliable fragment lost loses the packet).
constexpr size_t kFramesPerPacket = 1024 / BodyFrame::kWireBytes;
constexpr size_t kRestsPerMessage = 1024;
} // namespace

HostPhysics::HostPhysics(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostPhysics::OnRestAsked);
}

void HostPhysics::OnRestAsked(const BodyRestRequest& acMessage, ConnectionId_t aSender) noexcept
{
    if (!m_game.ReadBodies)
        return;
    std::vector<BodyKey> keys;
    for (const auto& cAsked : acMessage.Rests)
        keys.push_back(cAsked.Key);
    NotifyBodyRests rests;
    rests.Tick = m_lastTick;
    rests.Rests = m_game.ReadBodies(keys);
    spdlog::info("HostPhysics: {:x} moved {} by itself, gets the host's {}", aSender, WorldStateLog::List(acMessage.Rests, [](const BodyPose& acPose) {
                     return WorldStateLog::DescribeBody(acPose.Key);
                 }, 8), WorldStateLog::List(rests.Rests, [](const BodyPose& acPose) {
                     return std::format("at ({:.0f}, {:.0f}, {:.0f})", acPose.Position.x, acPose.Position.y, acPose.Position.z);
                 }, 8));
    if (!rests.Rests.empty())
        m_network.Send(aSender, rests, Net::kReliable);
}

void HostPhysics::Update(uint64_t aTick) noexcept
{
    SendBodies(aTick);
    m_lastTick = aTick;
}

void HostPhysics::SendBodies(uint64_t aTick) noexcept
{
    if (!m_game.TakeBodies)
        return;
    HostBodies bodies = m_game.TakeBodies();
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost)
        return;
    m_motions.Mark(bodies.Awake, aTick);
    for (const auto& cRest : bodies.Rested)
    {
        m_motions.Forget(cRest.Key);
        }

    for (const ConnectionId_t cGuest : m_players.Connections())
    {
        if (cGuest == cHost)
            continue;
        BodySends& sends = m_sends[cGuest];
        auto& sent = m_sentFrames[cGuest];
        std::vector<std::pair<BodyKey, uint32_t>> ended;
        for (const auto& cRest : bodies.Rested)
        {
            sends.Forget(cRest.Key);
            if (const auto it = sent.find(cRest.Key); it != sent.end())
            {
                ended.emplace_back(it->first, it->second);
                sent.erase(it);
            }
        }
        if (!ended.empty())
            spdlog::info("HostPhysics: {:x} got {}", cGuest, WorldStateLog::List(ended, [](const auto& acEnded) {
                             return std::format("{} frames of {}", acEnded.second, WorldStateLog::DescribeBody(acEnded.first));
                         }, 16));
        // Frames only of what is near its copy; rests go to every guest (below).
        const auto cPosition = m_game.ReadPlayerPosition ? m_game.ReadPlayerPosition(m_players.PlayerIdOf(cGuest)) : std::nullopt;
        const auto cDue = sends.Due(BodyRelevance::Near(bodies.Awake, cPosition), aTick);
        std::vector<BodyKey> started;
        for (const auto& cFrame : cDue)
            if (++sent[cFrame.Key] == 1)
                started.push_back(cFrame.Key);
        if (!started.empty())
            spdlog::info("HostPhysics: {:x} gets the frames of {} ({})", cGuest,
                         WorldStateLog::List(started, [](const BodyKey& acKey) { return WorldStateLog::DescribeBody(acKey); }, 16),
                         cPosition ? std::format("near its copy at ({:.0f}, {:.0f}, {:.0f})", cPosition->x, cPosition->y, cPosition->z)
                                   : std::string("no copy shown: all"));
        for (size_t first = 0; first < cDue.size(); first += kFramesPerPacket)
        {
            NotifyBodyFrames frames;
            frames.Tick = aTick;
            frames.Frames.assign(cDue.begin() + first, cDue.begin() + std::min(cDue.size(), first + kFramesPerPacket));
            m_network.Send(cGuest, frames, Net::kUnreliable);
        }
    }

    if (bodies.Rested.empty())
        return;
    spdlog::info("HostPhysics: sent {} rests: {}", bodies.Rested.size(), WorldStateLog::List(bodies.Rested, [](const BodyPose& acRest) {
                     return std::format("{} at ({:.0f}, {:.0f}, {:.0f})", WorldStateLog::DescribeBody(acRest.Key), acRest.Position.x, acRest.Position.y,
                                        acRest.Position.z);
                 }, 16));
    for (size_t first = 0; first < bodies.Rested.size(); first += kRestsPerMessage)
    {
        NotifyBodyRests rests;
        rests.Tick = aTick;
        rests.Rests.assign(bodies.Rested.begin() + first, bodies.Rested.begin() + std::min(bodies.Rested.size(), first + kRestsPerMessage));
        m_players.SendToOthers(rests, cHost, Net::kReliable);
    }
}

void HostPhysics::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_sends.erase(aPlayer);
    m_sentFrames.erase(aPlayer);
}
