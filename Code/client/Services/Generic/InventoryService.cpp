#include <Services/InventoryService.h>

#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyDrawWeapon.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>

#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Games/Overrides.h>
#include <set>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                    IActorBridge& aActorBridge, IFormBridge& aFormBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&InventoryService::OnUpdate>(this);
    m_inventoryConnection = m_dispatcher.sink<InventoryChangeEvent>().connect<&InventoryService::OnInventoryChangeEvent>(this);
    m_equipmentConnection = m_dispatcher.sink<EquipmentChangeEvent>().connect<&InventoryService::OnEquipmentChangeEvent>(this);
    m_inventoryChangeConnection = m_dispatcher.sink<NotifyInventoryChanges>().connect<&InventoryService::OnNotifyInventoryChanges>(this);
    m_equipmentChangeConnection = m_dispatcher.sink<NotifyEquipmentChanges>().connect<&InventoryService::OnNotifyEquipmentChanges>(this);
    m_equipmentSnapshotConnection = m_dispatcher.sink<NotifyEquipmentSnapshot>().connect<&InventoryService::OnNotifyEquipmentSnapshot>(this);
    m_drawWeaponConnection = m_dispatcher.sink<NotifyDrawWeapon>().connect<&InventoryService::OnNotifyDrawWeapon>(this);
    m_localComponentRemoved = m_world.on_destroy<LocalComponent>().connect<&InventoryService::OnLocalComponentRemoved>(this);
    m_remoteComponentRemoved = m_world.on_destroy<RemoteComponent>().connect<&InventoryService::OnRemoteComponentRemoved>(this);
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    RunWeaponStateUpdates();
    RunWeaponDrawConvergence(acUpdateEvent.Delta);
    RunEquipmentSnapshots();
    RunNakedNPCBugChecks();
}

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.FormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: : failed to find server id, target form id: {:X}, item id: {:X}, count: {}", __FUNCTION__, acEvent.FormId, acEvent.Item.BaseId.BaseId, acEvent.Item.Count);
        return;
    }

    RequestInventoryChanges request;
    request.ServerId = serverIdRes.value();
    request.Item = acEvent.Item;
    request.Drop = acEvent.Drop;
    request.UpdateClients = acEvent.UpdateClients;

    m_transport.Send(request);

    spdlog::info("Sending item request, item: {:X}, count: {}, target object: {:X}", acEvent.Item.BaseId.BaseId, acEvent.Item.Count, acEvent.FormId);
}

void InventoryService::OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.ActorId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: : failed to find server id, actor id: {:X}, item id: {:X}, isAmmo: {}, unequip: {}, slot: {:X}", __FUNCTION__, acEvent.ActorId, acEvent.ItemId, acEvent.IsAmmo, acEvent.Unequip, acEvent.EquipSlotId);
        return;
    }

    if (!m_actorBridge.ActorExists(acEvent.ActorId))
        return;

    auto& modSystem = World::Get().GetModSystem();

    RequestEquipmentChanges request;
    request.ServerId = serverIdRes.value();

    if (!modSystem.GetServerModId(acEvent.EquipSlotId, request.EquipSlotId))
        return;
    if (!modSystem.GetServerModId(acEvent.ItemId, request.ItemId))
        return;

    request.Count = acEvent.Count;
    request.Unequip = acEvent.Unequip;
    request.IsSpell = acEvent.IsSpell;
    request.IsShout = acEvent.IsShout;
    request.IsAmmo = acEvent.IsAmmo;
    request.CurrentInventory = m_formBridge.GetActorEquipment(acEvent.ActorId);

    m_transport.Send(request);

    spdlog::info("Sending equipment request, item: {:X}, count: {}, target object: {:X}", acEvent.ItemId, acEvent.Count, acEvent.ActorId);
}

void InventoryService::OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept
{
    if (acMessage.Drop)
    {
        const uint32_t cActorFormId = Utils::GetFormIdByServerId(acMessage.ServerId);
        if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId))
        {
            spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ServerId);
            return;
        }

        ScopedInventoryOverride _;

        m_formBridge.ActorDropOrPickUpObject(cActorFormId, acMessage.Item);
    }
    else
    {
        const uint32_t cObjectFormId = Utils::GetFormIdByServerId(acMessage.ServerId);
        if (cObjectFormId == 0 || !m_formBridge.FormExists(cObjectFormId))
            return;

        ScopedInventoryOverride _;

        m_formBridge.ObjectAddOrRemoveItem(cObjectFormId, acMessage.Item);
    }
}

void InventoryService::OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept
{
    const uint32_t cActorFormId = Utils::GetFormIdByServerId(acMessage.ServerId);
    if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId))
    {
        spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ServerId);
        return;
    }

    auto& modSystem = World::Get().GetModSystem();

    uint32_t itemId = modSystem.GetGameId(acMessage.ItemId);
    if (!m_formBridge.FormExists(itemId))
    {
        spdlog::error("Could not find inventory item {:X}:{:X}", acMessage.ItemId.ModId, acMessage.ItemId.BaseId);
        return;
    }

    uint32_t equipSlotId = modSystem.GetGameId(acMessage.EquipSlotId);

    // Determine slot index: 1 = right hand, 0 = left hand
    uint32_t slotId = m_formBridge.IsRightHandEquipSlot(equipSlotId) ? 1 : 0;

    if (acMessage.IsSpell)
    {
        if (acMessage.Unequip)
            m_formBridge.ActorUnequipSpell(cActorFormId, itemId, slotId);
        else
            m_formBridge.ActorEquipSpell(cActorFormId, itemId, slotId);
        return;
    }
    else if (acMessage.IsShout)
    {
        if (acMessage.Unequip)
            m_formBridge.ActorUnequipShout(cActorFormId, itemId);
        else
            m_formBridge.ActorEquipShout(cActorFormId, itemId);
        return;
    }

    if (acMessage.Unequip)
    {
        m_formBridge.ActorUnequipItem(cActorFormId, itemId, equipSlotId, acMessage.Count);
    }
    else
    {
        // Unequip all armor first, since the game won't auto unequip armor
        if (m_formBridge.GetFormType(itemId) == static_cast<uint8_t>(26)) // FormType::Armor = 26
        {
            Inventory wornArmor = m_formBridge.GetActorWornArmor(cActorFormId);
            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = modSystem.GetGameId(armor.BaseId);
                if (m_formBridge.FormExists(armorId))
                    m_formBridge.ActorUnequipItem(cActorFormId, armorId, equipSlotId, 1);
            }

            m_formBridge.ActorEquipItem(cActorFormId, itemId, equipSlotId, acMessage.Count);

            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = modSystem.GetGameId(armor.BaseId);
                if (m_formBridge.FormExists(armorId))
                    m_formBridge.ActorEquipItem(cActorFormId, armorId, equipSlotId, 1);
            }
        }
        else
        {
            m_formBridge.ActorEquipItem(cActorFormId, itemId, equipSlotId, acMessage.Count);
        }
    }
}

// Equipment changes are sent as events, so a copy that misses or undoes one would stay wrong
// forever. Like health, the owner re-sends the full worn set now and then and copies converge.
void InventoryService::RunEquipmentSnapshots() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 3s;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;
    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            continue;

        RequestEquipmentSnapshot request;
        request.ServerId = view.get<LocalComponent>(entity).Id;
        request.Equipment = m_formBridge.GetActorEquipment(cFormId);
        m_transport.Send(request);
    }
}

void InventoryService::OnNotifyEquipmentSnapshot(const NotifyEquipmentSnapshot& acMessage) noexcept
{
    const uint32_t cActorFormId = Utils::GetFormIdByServerId(acMessage.ServerId);
    if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId) || !m_actorBridge.IsActorCopy(cActorFormId))
        return;

    constexpr uint32_t cRightHandSlot = 0x13F42;
    constexpr uint32_t cLeftHandSlot = 0x13F43;
    constexpr uint8_t cArmorType = 26;
    auto& modSystem = World::Get().GetModSystem();

    // Worn items as (game form id, left hand) on each side.
    const auto worn = [&](const Inventory& acInventory) {
        std::set<std::pair<uint32_t, bool>> items;
        for (const auto& entry : acInventory.Entries)
            if (entry.IsWorn())
                if (const uint32_t cId = modSystem.GetGameId(entry.BaseId))
                    items.insert({cId, entry.ExtraWornLeft});
        return items;
    };
    const auto cWanted = worn(acMessage.Equipment);
    const auto cCurrent = worn(m_formBridge.GetActorEquipment(cActorFormId));

    const auto slotOf = [&](uint32_t aItemId, bool aLeft) -> uint32_t {
        if (m_formBridge.GetFormType(aItemId) == cArmorType)
            return 0;
        return aLeft ? cLeftHandSlot : cRightHandSlot;
    };

    for (const auto& [cItemId, cLeft] : cCurrent)
        if (!cWanted.count({cItemId, cLeft}))
        {
            spdlog::info("Equipment reconcile {:X}: unequip {:X}", cActorFormId, cItemId);
            m_formBridge.ActorUnequipItem(cActorFormId, cItemId, slotOf(cItemId, cLeft), 1);
        }
    for (const auto& [cItemId, cLeft] : cWanted)
        if (!cCurrent.count({cItemId, cLeft}) && m_formBridge.FormExists(cItemId))
        {
            spdlog::info("Equipment reconcile {:X}: equip {:X}", cActorFormId, cItemId);
            m_formBridge.ActorEquipItem(cActorFormId, cItemId, slotOf(cItemId, cLeft), 1);
        }
}

void InventoryService::RunWeaponStateUpdates() noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        // A dying actor's hands drop with its death, which its copies replay later: sent now, a copy would sheathe first.
        if (!m_actorBridge.ActorExists(cFormId) || m_actorBridge.IsActorDead(cFormId))
            continue;

        const bool cHandsUp = HandsUp(m_actorBridge.GetWeaponState(cFormId));
        const auto* pSent = m_world.try_get<LocalWeaponDrawComponent>(entity);
        if (pSent && pSent->HandsUp == cHandsUp)
            continue;

        m_world.emplace_or_replace<LocalWeaponDrawComponent>(entity, cHandsUp);

        DrawWeaponRequest request;
        request.Id = view.get<LocalComponent>(entity).Id;
        request.IsWeaponDrawn = cHandsUp;
        m_transport.Send(request);
    }
}

void InventoryService::OnNotifyDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept
{
    auto view = m_world.view<RemoteComponent>();
    for (auto entity : view)
        if (view.get<RemoteComponent>(entity).Id == acMessage.Id)
            m_world.emplace_or_replace<RemoteWeaponDrawComponent>(entity, acMessage.IsWeaponDrawn);
}

void InventoryService::RunWeaponDrawConvergence(double aDelta) noexcept
{
    // Long enough for an accepted replay to land first.
    constexpr double cDriftGrace = 0.25;

    auto view = m_world.view<FormIdComponent, RemoteAnimationComponent, RemoteWeaponDrawComponent>();
    for (auto entity : view)
    {
        auto& owner = view.get<RemoteWeaponDrawComponent>(entity);
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        if (!m_actorBridge.ActorExists(cFormId) || !m_actorBridge.ActorHas3D(cFormId) || m_actorBridge.IsActorDead(cFormId))
        {
            owner.DriftSeconds = 0.0;
            continue;
        }

        const WeaponState cState = m_actorBridge.GetWeaponState(cFormId);
        const bool cDrifted = view.get<RemoteAnimationComponent>(entity).TimePoints.empty() && !IsAnimating(cState) && ShowsHandsUp(cState) != owner.HandsUp;
        owner.DriftSeconds = cDrifted ? owner.DriftSeconds + aDelta : 0.0;
        if (owner.DriftSeconds < cDriftGrace)
            continue;

        spdlog::debug("Copy {:X} hands {} (weapon state {}) while its owner's are {}: converging", cFormId, ShowsHandsUp(cState) ? "up" : "down",
                      static_cast<int>(cState), owner.HandsUp ? "up" : "down");
        m_actorBridge.SetActorWeaponDrawnEx(cFormId, owner.HandsUp);
        owner.DriftSeconds = 0.0;
    }
}

void InventoryService::OnLocalComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    aRegistry.remove<LocalWeaponDrawComponent>(aEntity);
}

void InventoryService::OnRemoteComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    aRegistry.remove<RemoteWeaponDrawComponent>(aEntity);
}

void InventoryService::RunNakedNPCBugChecks() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent>();

    for (auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        const uint32_t formId = formIdComponent.Id;

        if (!m_actorBridge.ActorExists(formId))
            continue;

        if (m_actorBridge.IsOtherPlayer(formId))
            continue;

        if (m_actorBridge.IsActorDead(formId))
            continue;

        if (m_formBridge.ActorIsWearingBodyPiece(formId))
            continue;

        if (!m_formBridge.ActorShouldWearBodyPiece(formId))
            continue;

        // Don't broadcast changes, it'll just make things messier.
        ScopedEquipOverride seo;
        ScopedInventoryOverride sio;

        m_formBridge.ActorResetInventory(formId, false);
    }
}
