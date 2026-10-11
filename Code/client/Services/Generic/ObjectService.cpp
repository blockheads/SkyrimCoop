#include <Services/ObjectService.h>

#include <World.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
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

// FormType enum values used for object type checks (no SKSE headers needed — values are constants).
namespace
{
    constexpr uint8_t kFormTypeDoor      = 29; // FormType::Door
    constexpr uint8_t kFormTypeContainer = 28; // FormType::Container
}

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                              IFormBridge& aFormBridge, IActorBridge& aActorBridge, IEventBridge& aEventBridge)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
    , m_eventBridge(aEventBridge)
{
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&ObjectService::OnDisconnected>(this);
    m_onActivateConnection = aDispatcher.sink<ActivateEvent>().connect<&ObjectService::OnActivate>(this);
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&ObjectService::OnActivateNotify>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&ObjectService::OnLockChange>(this);
    m_lockChangeNotifyConnection = aDispatcher.sink<NotifyLockChange>().connect<&ObjectService::OnLockChangeNotify>(this);
    m_assignObjectConnection = aDispatcher.sink<AssignObjectsResponse>().connect<&ObjectService::OnAssignObjectsResponse>(this);
    m_scriptAnimationConnection = aDispatcher.sink<ScriptAnimationEvent>().connect<&ObjectService::OnScriptAnimationEvent>(this);
    m_scriptAnimationNotifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&ObjectService::OnNotifyScriptAnimation>(this);

    // Note: TESActivateEvent sink registration replaced with IEventBridge — ObjectService no longer
    // inherits BSTEventSink<TESActivateEvent>. The debug object component tracking was inside
    // #if ENVIRONMENT_DEBUG which is not compiled in release builds.
    // m_eventBridge.RegisterActivateSink(...) is available for future debug tracking if needed.
}

static bool IsPlayerHome(uint32_t aCellFormId, uint32_t aEncounterZoneFormId) noexcept
{
    // Only return true if cell has the NoResetZone encounter zone (0xf90b1)
    if (aEncounterZoneFormId == 0xf90b1)
    {
        switch (aCellFormId)
        {
        case 0xeec55: // one known exception: Sinderion's Field Lab
            return false;
        default: return true;
        }
    }
    return false;
}

bool ShouldSyncObject(uint32_t aObjectFormId) noexcept
{
    switch (aObjectFormId)
    {
    case 0x39CF1: // Don't sync the chest in the "Diplomatic Immunity" quest
        return false;
    case 0x3EF03: // ...as well as in the "No One Escapes Cidhna Mine" quest
        return false;
    default:
        return true;
    }
}

void ObjectService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // TODO(cosideci): clear object components
}

void ObjectService::OnAssignObjectsResponse(const AssignObjectsResponse& acMessage) noexcept
{
    for (const ObjectData& objectData : acMessage.Objects)
    {
        const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(objectData.Id);
        if (!m_formBridge.FormExists(cObjectId))
        {
            spdlog::error("Object not found for form id {:X}", objectData.Id);
            continue;
        }

        CreateObjectEntity(cObjectId, objectData.ServerId);

        if (objectData.IsSenderFirst)
            continue;

        if (objectData.CurrentLockData != LockData{})
        {
            if (!m_formBridge.ObjectHasLock(cObjectId))
            {
                if (!m_formBridge.SetObjectLock(cObjectId, objectData.CurrentLockData.IsLocked, objectData.CurrentLockData.LockLevel))
                    continue;
            }
            else
            {
                m_formBridge.SetObjectLock(cObjectId, objectData.CurrentLockData.IsLocked, objectData.CurrentLockData.LockLevel);
            }
            m_formBridge.NotifyObjectLockChange(cObjectId);
        }

        if (m_formBridge.IsObjectBaseFormType(cObjectId, kFormTypeContainer))
        {
            Inventory currentInventory = m_formBridge.GetObjectInventory(cObjectId);

            if (currentInventory.ContainsQuestItems())
                m_formBridge.SetObjectInventoryRetainingQuestItems(cObjectId, currentInventory, objectData.CurrentInventory);
            else
                m_formBridge.SetObjectInventory(cObjectId, objectData.CurrentInventory);
        }
    }
}

entt::entity ObjectService::CreateObjectEntity(const uint32_t acFormId, const uint32_t acServerId) noexcept
{
    const auto view = m_world.view<FormIdComponent, ObjectComponent>();

    auto it = std::find_if(view.begin(), view.end(), [acServerId, view](entt::entity entity) { return view.get<ObjectComponent>(entity).Id == acServerId; });

    if (it != view.end())
        return *it;

    entt::entity entity = m_world.create();
    spdlog::info("Created object entity, server id: {:X}, form id {:X}", acServerId, acFormId);

    m_world.emplace<FormIdComponent>(entity, acFormId);
    m_world.emplace<ObjectComponent>(entity, acServerId);

    return entity;
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

    if (m_formBridge.ObjectHasLock(acEvent.ObjectFormId))
    {
        if (m_formBridge.GetObjectLockLevel(acEvent.ObjectFormId) & 0xFF)
            return;
    }

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
