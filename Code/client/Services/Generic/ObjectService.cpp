#include <Services/ObjectService.h>

#include <World.h>
#include <Events/UpdateEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>

#include <Bridges/IFormBridge.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IEventBridge.h>

#include <inttypes.h>

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                              IFormBridge& aFormBridge, IActorBridge& aActorBridge, IEventBridge& aEventBridge)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
    , m_eventBridge(aEventBridge)
{
    m_onActivateConnection = aDispatcher.sink<ActivateEvent>().connect<&ObjectService::OnActivate>(this);
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&ObjectService::OnActivateNotify>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&ObjectService::OnLockChange>(this);
    m_lockChangeNotifyConnection = aDispatcher.sink<NotifyLockChange>().connect<&ObjectService::OnLockChangeNotify>(this);
    m_scriptAnimationConnection = aDispatcher.sink<ScriptAnimationEvent>().connect<&ObjectService::OnScriptAnimationEvent>(this);
    m_scriptAnimationNotifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&ObjectService::OnNotifyScriptAnimation>(this);

    // Note: TESActivateEvent sink registration replaced with IEventBridge — ObjectService no longer
    // inherits BSTEventSink<TESActivateEvent>. The debug object component tracking was inside
    // #if ENVIRONMENT_DEBUG which is not compiled in release builds.
    // m_eventBridge.RegisterActivateSink(...) is available for future debug tracking if needed.
}

void ObjectService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    if (acEvent.ActivateFlag)
    {
        // Re-activate: delegate to bridge using form IDs from the event struct.
        m_formBridge.ActivateObjectFromActor(acEvent.ObjectFormId, acEvent.ActivatorFormId,
                                              acEvent.PreActivationOpenStateValue);
    }

    if (!m_transport.IsConnected())
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

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*pEntity);
    if (!serverIdRes.has_value())
        return;

    request.ActivatorId = serverIdRes.value();
    request.PreActivationOpenState = acEvent.PreActivationOpenStateValue;

    m_transport.Send(request);
}

void ObjectService::OnActivateNotify(const NotifyActivate& acMessage) noexcept
{
    const uint32_t cActorFormId = Utils::GetFormIdByServerId(acMessage.ActivatorId);
    if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId))
    {
        spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ActivatorId);
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

void ObjectService::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    LockChangeRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.FormId, request.Id))
    {
        spdlog::error("Server form id for lock object not found, form id: {:X}", acEvent.FormId);
        return;
    }

    const uint32_t cParentCellId = m_formBridge.GetObjectParentCellId(acEvent.FormId);
    if (cParentCellId == 0)
    {
        spdlog::error("Activated object has no parent cell: {:X}", acEvent.FormId);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(cParentCellId, request.CellId))
    {
        spdlog::error("Server cell id for cell not found, cell form id: {:X}", cParentCellId);
        return;
    }

    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;

    m_transport.Send(request);
}

void ObjectService::OnLockChangeNotify(const NotifyLockChange& acMessage) noexcept
{
    const auto cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    if (cObjectId == 0)
    {
        spdlog::error("Failed to retrieve object id to (un)lock.");
        return;
    }

    if (!m_formBridge.FormExists(cObjectId))
    {
        spdlog::error("Failed to retrieve object to (un)lock.");
        return;
    }

    if (!acMessage.IsLocked)
    {
        if (!m_formBridge.ObjectHasLock(cObjectId) || !m_formBridge.ObjectIsLocked(cObjectId))
            return;
    }

    if (!m_formBridge.ObjectHasLock(cObjectId) && acMessage.IsLocked)
    {
        if (!m_formBridge.SetObjectLock(cObjectId, true, acMessage.LockLevel))
        {
            spdlog::error("Failed to create lock for object form id {:X}", cObjectId);
            return;
        }
    }
    else
    {
        m_formBridge.SetObjectLock(cObjectId, acMessage.IsLocked, acMessage.LockLevel);
    }

    m_formBridge.NotifyObjectLockChange(cObjectId);
}

void ObjectService::OnScriptAnimationEvent(const ScriptAnimationEvent& acEvent) noexcept
{
    ScriptAnimationRequest request{};
    request.FormID = acEvent.FormID;
    request.Animation = acEvent.Animation;
    request.EventName = acEvent.EventName;

    m_transport.Send(request);
}

void ObjectService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
{
    if (acMessage.FormID == 0)
        return;

    if (!m_formBridge.FormExists(acMessage.FormID))
    {
        spdlog::error("Failed to fetch notify script animation object, form id: {:X}", acMessage.FormID);
        return;
    }

    m_formBridge.PlayObjectAnimation(acMessage.FormID,
                                      std::string(acMessage.Animation.c_str()),
                                      std::string(acMessage.EventName.c_str()));
}
