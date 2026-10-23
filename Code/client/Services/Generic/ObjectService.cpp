#include <Services/ObjectService.h>

#include <World.h>
#include <Events/UpdateEvent.h>
#include <Events/ActivateEvent.h>
#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>

#include <Bridges/IFormBridge.h>
#include <Bridges/IActorBridge.h>

#include <inttypes.h>
#include <Components/LocalComponent.h>
#include <SessionIds.h>
#include <Components/FormIdComponent.h>

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                              IFormBridge& aFormBridge, IActorBridge& aActorBridge)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
{
    m_onActivateConnection = aDispatcher.sink<ActivateEvent>().connect<&ObjectService::OnActivate>(this);
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&ObjectService::OnActivateNotify>(this);
}

void ObjectService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    if (acEvent.ActivateFlag)
    {
        // Re-activate: delegate to bridge using form IDs from the event struct.
        m_formBridge.ActivateObjectFromActor(acEvent.ObjectFormId, acEvent.ActivatorFormId,
                                              acEvent.PreActivationOpenStateValue);
    }

    // A world script's door is the host's world: HostWorldState sends the host's (step 5).
    if (!m_transport.IsConnected() || acEvent.ByReference)
        return;

    // A locked object's activation is a lockpick attempt, not an opening. Its lock level stays set
    // once it is unlocked: that door opens like any other.
    if (m_formBridge.ObjectHasLock(acEvent.ObjectFormId) && m_formBridge.ObjectIsLocked(acEvent.ObjectFormId))
        return;

    ActivateRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.ObjectFormId, request.Id))
    {
        spdlog::error("Server form id not found for object form id {:X}", acEvent.ObjectFormId);
        return;
    }

    if (acEvent.ObjectParentCellFormId == 0)
    {
        spdlog::error("Activated object has no parent cell: {:X}", acEvent.ObjectFormId);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(acEvent.ObjectParentCellFormId, request.CellId))
    {
        spdlog::error("Server cell id not found for cell form id {:X}", acEvent.ObjectParentCellFormId);
        return;
    }

    auto view = m_world.view<FormIdComponent>();
    const auto pEntity = std::find_if(std::begin(view), std::end(view), [id = acEvent.ActivatorFormId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    // Only what our own actors do: another game says what its actors did. (Replays do not come
    // back through here: they call the engine's activation past our hook.)
    if (pEntity == std::end(view) || !m_world.all_of<LocalComponent>(*pEntity))
        return;

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(*pEntity);
    if (!sessionIdRes.has_value())
        return;

    request.ActivatorId = sessionIdRes.value();
    request.PreActivationOpenState = acEvent.PreActivationOpenStateValue;

    m_transport.Send(request);
}

void ObjectService::OnActivateNotify(const NotifyActivate& acMessage) noexcept
{
    const uint32_t cActorFormId = Utils::GetFormIdBySessionId(acMessage.ActivatorId);
    if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId))
    {
        spdlog::error("{}: could not find actor session id {:X}", __FUNCTION__, acMessage.ActivatorId);
        return;
    }

    const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    if (!m_formBridge.FormExists(cObjectId))
    {
        spdlog::error("Failed to retrieve object to activate.");
        return;
    }

    m_formBridge.ActivateObjectFromActor(cObjectId, cActorFormId,
                                          acMessage.PreActivationOpenState);
}
