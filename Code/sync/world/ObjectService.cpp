#include <world/ObjectService.h>

#include <core/World.h>
#include <core/UpdateEvent.h>
#include <world/ActivateEvent.h>
#include <world/LidUnposedEvent.h>
#include <Messages/ActivateRequest.h>

#include <ports/IFormBridge.h>
#include <ports/IActorBridge.h>

#include <inttypes.h>
#include <core/LocalComponent.h>
#include <core/SessionIds.h>
#include <core/FormIdComponent.h>
#include <core/FormIds.h>

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                              IFormBridge& aFormBridge, IActorBridge& aActorBridge)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
{
    Listen<ActivateEvent, &ObjectService::OnActivate>(m_connections, aDispatcher, this);
    Listen<LidUnposedEvent, &ObjectService::OnLidUnposed>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &ObjectService::OnUpdate>(m_connections, aDispatcher, this);
}

void ObjectService::OnLidUnposed(const LidUnposedEvent& acEvent) noexcept
{
    m_lidsToPose.insert(acEvent.FormId);
}

void ObjectService::OnUpdate(const UpdateEvent&) noexcept
{
    std::erase_if(m_lidsToPose, [this](uint32_t aObject) {
        if (!m_formBridge.PoseObjectOpenState(aObject))
            return false;
        spdlog::debug("ObjectService: {:X}'s lid posed at its open state {}", aObject, m_formBridge.GetObjectOpenState(aObject));
        return true;
    });
}

void ObjectService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    // A world script's door is the host's world: HostWorldState sends the host's (step 5).
    // Activating a mount rides it (Character::ActivateMount 37864): the ride goes as a MountRequest. Replayed, it
    // would start a mount package on the activator's copy, which no copy runs.
    if (!m_transport.IsConnected() || acEvent.ByReference || m_actorBridge.IsActorMount(acEvent.ObjectFormId))
        return;

    // A locked object's activation is a lockpick attempt, not an opening. Its lock level stays set
    // once it is unlocked: that door opens like any other.
    if (m_formBridge.ObjectHasLock(acEvent.ObjectFormId) && m_formBridge.ObjectIsLocked(acEvent.ObjectFormId))
        return;

    ActivateRequest request;

    const auto cObject = ToSession(acEvent.ObjectFormId);
    const auto cNpc = m_actorBridge.ActorExists(acEvent.ObjectFormId) ? Utils::GetSessionIdByFormId(acEvent.ObjectFormId) : std::nullopt;
    if (!cObject && !cNpc)
        return;
    if (cNpc)
        request.NpcId = *cNpc;
    else
        request.Id = *cObject;

    if (acEvent.ObjectParentCellFormId == 0)
    {
        spdlog::error("Activated object has no parent cell: {:X}", acEvent.ObjectFormId);
        return;
    }

    const auto cCell = ToSession(acEvent.ObjectParentCellFormId);
    if (!cCell)
        return;
    request.CellId = *cCell;

    const auto cEntity = m_world.GetActorIndex().ByForm(acEvent.ActivatorFormId);

    // Only what our own actors do: another game says what its actors did. (Replays do not come
    // back through here: they call the engine's activation past our hook.)
    if (cEntity == entt::null || !m_world.all_of<LocalComponent>(cEntity))
        return;

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(cEntity);
    if (!sessionIdRes.has_value())
        return;

    request.ActivatorId = sessionIdRes.value();

    m_transport.Send(request);
}
