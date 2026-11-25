#include <Services/Host/HostGraph.h>

#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostRouter.h>
#include <Messages/Relayed.h>

HostGraph::HostGraph(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
{
    using enum HostRouter::Delivery;
    aRouter.On(*this, &HostGraph::OnGraphOps);
    aRouter.Relay<GraphSyncRequest, NotifyGraphSync>(ToOwner, &GraphSyncRequest::Id);
    aRouter.Relay<GraphSyncPointRequest, NotifyGraphSyncPoint>(FromOwner, &GraphSyncPointRequest::Id);
}

void HostGraph::OnGraphOps(const GraphOpsRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyGraphOps notify;
    notify.Ops.Frames = acRequest.Ops.Frames;
    for (const auto& cRun : acRequest.Ops.Runs)
        if (m_characters.OwnerOf(cRun.ActorId) == aSender)
            notify.Ops.Runs.push_back(cRun);
    if (!notify.Ops.Runs.empty())
        m_players.SendToOthers(notify, aSender, Net::kReliable);
}
