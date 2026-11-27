// An owner's graph stream out to its copies, and its actions queued for the next snapshot.
#include <animation/GraphStreamService.h>

#include <animation/AnimationSystem.h>
#include <animation/GraphStream.h>
#include <animation/LocalAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <core/TransportService.h>
#include <core/World.h>
#include <ports/IActorBridge.h>

#include <Messages/Relayed.h>
#include <Structs/ActionEvent.h>

GraphStreamService::GraphStreamService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    Listen<ActionEvent, &GraphStreamService::OnActionEvent>(m_connections, aDispatcher, this);
    Listen<&GraphStreamService::OnStreamedRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
    Listen<&GraphStreamService::OnStreamedRemoved>(m_connections, m_world.on_destroy<RemoteComponent>(), this);
    Listen<NotifyGraphOps, &GraphStreamService::OnGraphOps>(m_connections, aDispatcher, this);
    Listen<NotifyGraphSync, &GraphStreamService::OnGraphSync>(m_connections, aDispatcher, this);
    Listen<NotifyGraphSyncPoint, &GraphStreamService::OnGraphSyncPoint>(m_connections, aDispatcher, this);
}

void GraphStreamService::OnStreamedRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept
{
    if (const auto* pForm = aRegistry.try_get<FormIdComponent>(aEntity))
        m_world.GetGraphStreams().Forget(pForm->Id);
}

void GraphStreamService::OnGraphOps(const NotifyGraphOps& acMessage) const noexcept
{
    auto& streams = m_world.GetGraphStreams();
    const auto& actors = m_world.GetActorIndex();
    for (const auto& cRun : acMessage.Ops.Runs)
    {
        const auto cCopy = actors.BySession<RemoteComponent, FormIdComponent>(cRun.ActorId);
        if (cCopy == entt::null)
            continue;
        const uint32_t cFormId = m_world.get<FormIdComponent>(cCopy).Id;
        // A copy that died off its stream (a pair with this game's actor) stays off it.
        if (m_actorBridge.IsActorDead(cFormId) && !streams.OnStream(cFormId))
            continue;
        streams.Feed(cFormId, cRun, acMessage.Ops.Frames);
        if (streams.ShouldAsk(cFormId))
        {
            GraphSyncRequest ask;
            ask.Id = cRun.ActorId;
            m_transport.Send(ask);
        }
    }
}

void GraphStreamService::OnGraphSync(const NotifyGraphSync& acMessage) const noexcept
{
    if (const auto cEntity = m_world.GetActorIndex().BySession<LocalComponent, FormIdComponent>(acMessage.Id); cEntity != entt::null)
        m_world.GetGraphStreams().WantPoint(m_world.get<FormIdComponent>(cEntity).Id);
}

void GraphStreamService::OnGraphSyncPoint(const NotifyGraphSyncPoint& acMessage) const noexcept
{
    const auto cCopy = m_world.GetActorIndex().BySession<RemoteComponent, FormIdComponent>(acMessage.Id);
    if (cCopy == entt::null)
        return;
    const uint32_t cFormId = m_world.get<FormIdComponent>(cCopy).Id;
    spdlog::info("Copy {:X} starts on its owner's graph stream at update {} ({} clip sets held)", cFormId, acMessage.Update, acMessage.ClipSets.size());
    m_world.GetGraphStreams().Load(cFormId, acMessage);
}

void GraphStreamService::Send() const noexcept
{
    auto& streams = m_world.GetGraphStreams();
    if (auto ops = streams.TakeOps())
    {
        GraphOpsRequest request;
        request.Ops = std::move(*ops);
        m_transport.Send(request);
    }
    for (auto& point : streams.TakePoints())
    {
        GraphSyncPointRequest request;
        static_cast<Payloads::GraphSyncPoint&>(request) = std::move(point);
        m_transport.Send(request);
    }
}

void GraphStreamService::OnActionEvent(const ActionEvent& acActionEvent) const noexcept
{
    if (const auto cEntity = m_world.GetActorIndex().ByForm<LocalAnimationComponent>(acActionEvent.ActorId); cEntity != entt::null)
    {
        auto& localComponent = m_world.get<LocalAnimationComponent>(cEntity);

        ActionEvent action = acActionEvent;
        AnimationSystem::TargetToNetwork(action);
        localComponent.Append(action);
    }
}

