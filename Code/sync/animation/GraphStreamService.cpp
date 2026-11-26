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
    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (const auto& cRun : acMessage.Ops.Runs)
        for (const auto cEntity : view)
            if (view.get<RemoteComponent>(cEntity).Id == cRun.ActorId)
            {
                const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id;
                // A copy that died off its stream (a pair with this game's actor) stays off it.
                if (m_actorBridge.IsActorDead(cFormId) && !streams.OnStream(cFormId))
                    break;
                streams.Feed(cFormId, cRun, acMessage.Ops.Frames);
                if (streams.ShouldAsk(cFormId))
                {
                    GraphSyncRequest ask;
                    ask.Id = cRun.ActorId;
                    m_transport.Send(ask);
                }
                break;
            }
}

void GraphStreamService::OnGraphSync(const NotifyGraphSync& acMessage) const noexcept
{
    auto view = m_world.view<LocalComponent, FormIdComponent>();
    for (const auto cEntity : view)
        if (view.get<LocalComponent>(cEntity).Id == acMessage.Id)
            m_world.GetGraphStreams().WantPoint(view.get<FormIdComponent>(cEntity).Id);
}

void GraphStreamService::OnGraphSyncPoint(const NotifyGraphSyncPoint& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (const auto cEntity : view)
        if (view.get<RemoteComponent>(cEntity).Id == acMessage.Id)
        {
            spdlog::info("Copy {:X} starts on its owner's graph stream at update {} ({} clip sets held)", view.get<FormIdComponent>(cEntity).Id, acMessage.Update, acMessage.ClipSets.size());
            m_world.GetGraphStreams().Load(view.get<FormIdComponent>(cEntity).Id, acMessage);
        }
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
    auto view = m_world.view<LocalAnimationComponent, FormIdComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        auto& localComponent = view.get<LocalAnimationComponent>(*itor);

        ActionEvent action = acActionEvent;
        AnimationSystem::TargetToNetwork(action);
        localComponent.Append(action);
    }
}

