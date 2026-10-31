#include <Services/InventoryService.h>
#include <Games/EquipmentRepair.h>
#include <Events/PlayerProgressEvent.h>
#include <Bridges/IPlayerBridge.h>

#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/Relayed.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/DisconnectedEvent.h>

#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Games/Overrides.h>
#include <Games/PartyItems.h>
#include <set>
#include <Components/LocalComponent.h>
#include <Components/LocalWeaponDrawComponent.h>
#include <Components/RemoteAnimationComponent.h>
#include <Components/RemoteComponent.h>
#include <Components/RemoteWeaponDrawComponent.h>
#include <SessionIds.h>
#include <Components/FormIdComponent.h>
#include <Components/WaitingFor3D.h>
#include <FormIds.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                    IActorBridge& aActorBridge, IFormBridge& aFormBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
{
    Listen<UpdateEvent, &InventoryService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<InventoryChangeEvent, &InventoryService::OnInventoryChangeEvent>(m_connections, m_dispatcher, this);
    Listen<EquipmentChangeEvent, &InventoryService::OnEquipmentChangeEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyInventoryChanges, &InventoryService::OnNotifyInventoryChanges>(m_connections, m_dispatcher, this);
    Listen<NotifyEquipmentChanges, &InventoryService::OnNotifyEquipmentChanges>(m_connections, m_dispatcher, this);
    Listen<NotifyEquipmentSnapshot, &InventoryService::OnNotifyEquipmentSnapshot>(m_connections, m_dispatcher, this);
    Listen<NotifyDrawWeapon, &InventoryService::OnNotifyDrawWeapon>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyGold, &InventoryService::OnNotifyPartyGold>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyItem, &InventoryService::OnNotifyPartyItem>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyItems, &InventoryService::OnNotifyPartyItems>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &InventoryService::OnDisconnected>(m_connections, m_dispatcher, this);
    Listen<&InventoryService::OnSpawnItemsApplied>(m_connections, m_world.on_destroy<WaitingFor3D>(), this);
    Listen<&InventoryService::OnLocalComponentRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
    Listen<&InventoryService::OnRemoteComponentRemoved>(m_connections, m_world.on_destroy<RemoteComponent>(), this);
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

    RequestInventoryChanges request;
    request.Item = acEvent.Item;
    request.Drop = acEvent.Drop;
    request.UpdateClients = acEvent.UpdateClients;

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(*iter);
    if (!sessionIdRes.has_value())
    {
        spdlog::error("{}: : failed to find session id, target form id: {:X}, item id: {:X}, count: {}", __FUNCTION__, acEvent.FormId, acEvent.Item.BaseId.BaseId, acEvent.Item.Count);
        return;
    }
    request.SessionId = sessionIdRes.value();

    // A key, a quest item or the story's gift arriving on a player is the party's (party-items.md): this game takes its
    // own copy back, and the host gives every player the tagged one.
    const bool cPlayer = acEvent.FormId == 0x14 || m_actorBridge.IsOtherPlayer(acEvent.FormId);
    const uint32_t cItemId = ToGame(acEvent.Item.BaseId);
    if (cPlayer && !acEvent.Drop && acEvent.Item.Count > 0 && !acEvent.Item.ExtraName.starts_with(PartyItems::cTag) &&
        PartyItems::ArrivesAsParty(m_formBridge.GetFormType(cItemId), acEvent.Item.IsQuestItem, acEvent.StoryGiven))
    {
        request.NewPartyItem = true;
        request.StoryGiven = acEvent.StoryGiven;
        if (!acEvent.Item.IsQuestItem)
        {
            Inventory::Entry own = acEvent.Item;
            own.Count = -own.Count;
            ScopedInventoryOverride _;
            m_formBridge.ObjectAddOrRemoveItem(acEvent.FormId, own);
            request.Item.ExtraName = PartyItems::Tagged(acEvent.Item.ExtraName, m_formBridge.GetFormName(cItemId));
            request.Item.ExtraWorn = request.Item.ExtraWornLeft = false;
        }
    }

    m_transport.Send(request);

    spdlog::info("Sending item request, item: {:X}, count: {}, target object: {:X}{}{}", acEvent.Item.BaseId.BaseId, acEvent.Item.Count,
                 acEvent.FormId, request.Item.ExtraName.empty() ? "" : std::format(", name '{}'", request.Item.ExtraName),
                 request.NewPartyItem ? ", a new party item" : "");
}

void InventoryService::OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.ActorId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(*iter);
    if (!sessionIdRes.has_value())
    {
        spdlog::error("{}: : failed to find session id, actor id: {:X}, item id: {:X}, isAmmo: {}, unequip: {}, slot: {:X}", __FUNCTION__, acEvent.ActorId, acEvent.ItemId, acEvent.IsAmmo, acEvent.Unequip, acEvent.EquipSlotId);
        return;
    }

    if (!m_actorBridge.ActorExists(acEvent.ActorId))
        return;

    RequestEquipmentChanges request;
    request.SessionId = *sessionIdRes;

    const auto cSlot = ToSession(acEvent.EquipSlotId);
    const auto cItem = ToSession(acEvent.ItemId);
    if (!cSlot || !cItem)
        return;
    request.EquipSlotId = *cSlot;
    request.ItemId = *cItem;

    request.Count = acEvent.Count;
    request.Unequip = acEvent.Unequip;
    request.IsSpell = acEvent.IsSpell;
    request.IsShout = acEvent.IsShout;
    request.IsAmmo = acEvent.IsAmmo;
    request.CurrentInventory = m_formBridge.GetActorEquipment(acEvent.ActorId);
    m_equipmentSnapshots.OnChange();

    m_transport.Send(request);

    spdlog::info("Sending equipment request, item: {:X}, count: {}, target object: {:X}", acEvent.ItemId, acEvent.Count, acEvent.ActorId);
}

void InventoryService::OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept
{
    if (acMessage.Drop)
    {
        const uint32_t cActorFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
        if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId))
        {
            spdlog::error("{}: could not find actor session id {:X}", __FUNCTION__, acMessage.SessionId);
            return;
        }

        ScopedInventoryOverride _;

        m_formBridge.ActorDropOrPickUpObject(cActorFormId, acMessage.Item);
    }
    else
    {
        const uint32_t cObjectFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
        if (cObjectFormId == 0 || !m_formBridge.FormExists(cObjectFormId))
            return;

        ScopedInventoryOverride _;

        m_formBridge.ObjectAddOrRemoveItem(cObjectFormId, acMessage.Item);
        if (cObjectFormId == m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId())
            m_dispatcher.trigger(PlayerProgressEvent{});
    }
}

void InventoryService::OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept
{
    const uint32_t cActorFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
    if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId))
    {
        spdlog::error("{}: could not find actor session id {:X}", __FUNCTION__, acMessage.SessionId);
        return;
    }

    uint32_t itemId = ToGame(acMessage.ItemId);
    if (!m_formBridge.FormExists(itemId))
    {
        spdlog::error("Could not find inventory item {:X}:{:X}", acMessage.ItemId.ModId, acMessage.ItemId.BaseId);
        return;
    }

    uint32_t equipSlotId = ToGame(acMessage.EquipSlotId);

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
        CarryForEquip(cActorFormId, acMessage.ItemId);
        // Unequip all armor first, since the game won't auto unequip armor
        if (m_formBridge.GetFormType(itemId) == static_cast<uint8_t>(26)) // FormType::Armor = 26
        {
            Inventory wornArmor = m_formBridge.GetActorWornArmor(cActorFormId);
            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = ToGame(armor.BaseId);
                if (m_formBridge.FormExists(armorId))
                    m_formBridge.ActorUnequipItem(cActorFormId, armorId, equipSlotId, 1);
            }

            m_formBridge.ActorEquipItem(cActorFormId, itemId, equipSlotId, acMessage.Count);

            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = ToGame(armor.BaseId);
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

    // The hook's event precedes the native equip; read the completed set on the next update.
    if (!m_equipmentSnapshots.SendAt(std::chrono::steady_clock::now()))
        return;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            continue;

        RequestEquipmentSnapshot request;
        request.SessionId = view.get<LocalComponent>(entity).Id;
        request.Equipment = m_formBridge.GetActorEquipment(cFormId);
        m_transport.Send(request);
    }
}

void InventoryService::OnNotifyEquipmentSnapshot(const NotifyEquipmentSnapshot& acMessage) noexcept
{
    const uint32_t cActorFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
    if (cActorFormId == 0 || !m_actorBridge.ActorExists(cActorFormId) || !m_actorBridge.IsActorCopy(cActorFormId))
        return;

    // Worn items as (game form id, left hand) on each side.
    const auto worn = [&](const Inventory& acInventory) {
        std::set<std::pair<uint32_t, bool>> items;
        for (const auto& entry : acInventory.Entries)
            if (entry.IsWorn())
                if (const uint32_t cId = ToGame(entry.BaseId))
                    items.insert({cId, entry.ExtraWornLeft});
        return items;
    };
    const auto cWanted = worn(acMessage.Equipment);
    const auto cCurrent = worn(m_formBridge.GetActorEquipment(cActorFormId));

    EquipmentRepair::Apply(cWanted, cCurrent,
        [&] {
            spdlog::info("Equipment reconcile {:X}: reset worn set", cActorFormId);
            m_formBridge.ActorResetEquipment(cActorFormId, acMessage.Equipment.CurrentMagicEquipment);
        },
        [&](uint32_t aItemId, bool aLeft) {
            if (!m_formBridge.FormExists(aItemId))
                return;
            for (const auto& cEntry : acMessage.Equipment.Entries)
                if (ToGame(cEntry.BaseId) == aItemId)
                {
                    CarryForEquip(cActorFormId, cEntry.BaseId);
                    break;
                }
            m_formBridge.ActorEquipItem(cActorFormId, aItemId, EquipmentRepair::EquipSlot(m_formBridge.GetFormType(aItemId), aLeft), 1);
        });
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

void InventoryService::OnNotifyPartyGold(const NotifyPartyGold& acMessage) noexcept
{
    m_partyGold = acMessage.Gold;
    SetGold(0x14, acMessage.Gold);
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id; m_actorBridge.IsOtherPlayer(cFormId))
            SetGold(cFormId, acMessage.Gold);
}

void InventoryService::OnNotifyPartyItem(const NotifyPartyItem& acMessage) noexcept
{
    if (m_partyItems)
    {
        std::erase_if(*m_partyItems, [&](const Inventory::Entry& acItem) { return PartyItems::SameStack(acMessage.Item, acItem); });
        if (acMessage.Item.Count > 0)
            m_partyItems->push_back(acMessage.Item);
    }
    SetPartyItem(0x14, acMessage.Item);
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id; m_actorBridge.IsOtherPlayer(cFormId))
            SetPartyItem(cFormId, acMessage.Item);
    if (acMessage.Shown)
        m_world.ctx().at<IPlayerBridge&>().ShowItemChangeMessage(ToGame(acMessage.Item.BaseId), acMessage.Shown);
}

// That stack (its base and name) set to the party's count, silently.
void InventoryService::SetPartyItem(uint32_t aActorFormId, const Inventory::Entry& acItem) const noexcept
{
    if (!m_actorBridge.ActorExists(aActorFormId))
        return;
    Inventory::Entry change = acItem;
    change.Count = acItem.Count - PartyItemCount(aActorFormId, acItem);
    AddPartyItem(aActorFormId, change);
}

void InventoryService::OnNotifyPartyItems(const NotifyPartyItems& acMessage) noexcept
{
    m_partyItems = acMessage.Items;
    SetPartyItems(0x14, acMessage.Items);
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id; m_actorBridge.IsOtherPlayer(cFormId))
            SetPartyItems(cFormId, acMessage.Items);
}

void InventoryService::SetPartyItems(uint32_t aActorFormId, const std::vector<Inventory::Entry>& acItems) const noexcept
{
    if (!m_actorBridge.ActorExists(aActorFormId))
        return;
    const auto cListed = [&](const Inventory::Entry& acStack) {
        return std::ranges::any_of(acItems, [&](const Inventory::Entry& acItem) { return PartyItems::SameStack(acItem, acStack); });
    };
    for (Inventory::Entry stale : PartyItemsOf(aActorFormId))
        if (!stale.IsQuestItem && !cListed(stale))
        {
            stale.Count = -stale.Count;
            AddPartyItem(aActorFormId, stale);
        }
    for (const Inventory::Entry& cItem : acItems)
        SetPartyItem(aActorFormId, cItem);
}

std::vector<Inventory::Entry> InventoryService::PartyItemsOf(uint32_t aActorFormId) const noexcept
{
    std::vector<Inventory::Entry> stacks;
    for (const auto& cEntry : m_actorBridge.GetActorInventory(aActorFormId).Entries)
    {
        if (!PartyItems::IsParty(cEntry) || cEntry.Count <= 0)
            continue;
        Inventory::Entry stack = cEntry;
        if (stack.IsQuestItem && stack.ExtraName.empty()) // its copies elsewhere are named with the tag
            stack.ExtraName = PartyItems::Tagged("", m_formBridge.GetFormName(ToGame(stack.BaseId)));
        const auto cSame = std::ranges::find_if(stacks, [&](const Inventory::Entry& acParty) { return PartyItems::SameStack(acParty, cEntry); });
        if (cSame != stacks.end())
            cSame->Count += cEntry.Count;
        else
            stacks.push_back(stack);
    }
    return stacks;
}

int32_t InventoryService::PartyItemCount(uint32_t aActorFormId, const Inventory::Entry& acItem) const noexcept
{
    int32_t carried = 0;
    for (const auto& cEntry : m_actorBridge.GetActorInventory(aActorFormId).Entries)
        if (PartyItems::SameStack(acItem, cEntry))
            carried += cEntry.Count;
    return carried;
}

void InventoryService::AddPartyItem(uint32_t aActorFormId, const Inventory::Entry& acItem) const noexcept
{
    if (acItem.Count == 0)
        return;
    Inventory::Entry change = acItem;
    const int32_t cCarried = PartyItemCount(aActorFormId, acItem);
    change.Count = std::max(acItem.Count, -cCarried); // a removal takes at most all
    change.IsQuestItem = false; // a copy: only the holder's stack is the engine's quest item
    {
        ScopedInventoryOverride _;
        m_formBridge.ObjectAddOrRemoveItem(aActorFormId, change);
    }
    const int32_t cNow = PartyItemCount(aActorFormId, acItem);
    spdlog::info("Party item {:X} '{}' on {:X}: carried {}, asked {}, changed {}, now {}", acItem.BaseId.LogFormat(), acItem.ExtraName,
                 aActorFormId, cCarried, acItem.Count, change.Count, cNow);
    if (cNow != cCarried + change.Count)
    {
        // The engine matched no stack: what was asked, and what is carried (extra data and all).
        const auto cDescribe = [](const Inventory::Entry& acEntry) {
            return std::format("x{} health {} charge {} ench {:X}/{} poison {:X} soul {} worn {}/{} quest {} name '{}'", acEntry.Count,
                               acEntry.ExtraHealth, acEntry.ExtraCharge, acEntry.ExtraEnchantId.LogFormat(), acEntry.ExtraEnchantCharge,
                               acEntry.ExtraPoisonId.LogFormat(), acEntry.ExtraSoulLevel, acEntry.ExtraWorn, acEntry.ExtraWornLeft,
                               acEntry.IsQuestItem, acEntry.ExtraName);
        };
        spdlog::warn("Party item {:X} on {:X} did not change: asked {}", acItem.BaseId.LogFormat(), aActorFormId, cDescribe(change));
        for (const auto& cEntry : m_actorBridge.GetActorInventory(aActorFormId).Entries)
            if (cEntry.BaseId == acItem.BaseId)
                spdlog::warn("  carried {}", cDescribe(cEntry));
    }
}

std::string InventoryService::ItemName(const GameId& acItem) const noexcept
{
    return m_formBridge.GetFormName(ToGame(acItem));
}

uint32_t InventoryService::GoldOf(uint32_t aActorFormId) const noexcept
{
    int64_t gold = 0;
    for (const auto& cEntry : m_actorBridge.GetActorInventory(aActorFormId).Entries)
        if (cEntry.BaseId == Payloads::PartyGold::kGold)
            gold += cEntry.Count;
    return static_cast<uint32_t>(std::max<int64_t>(gold, 0));
}

// The owner wears it, so it has it: an item its engine gave back with no container event (an outfit put on again
// after removeallitems) is added to the copy first. EquipManager::Equip does nothing for an item not carried.
void InventoryService::CarryForEquip(uint32_t aActorFormId, const GameId& acItem) const noexcept
{
    if (!m_actorBridge.IsActorCopy(aActorFormId))
        return;
    for (const auto& cEntry : m_actorBridge.GetActorInventory(aActorFormId).Entries)
        if (cEntry.BaseId == acItem && cEntry.Count > 0)
            return;
    Inventory::Entry carried;
    carried.BaseId = acItem;
    carried.Count = 1;
    ScopedInventoryOverride _;
    m_formBridge.ObjectAddOrRemoveItem(aActorFormId, carried);
}

void InventoryService::SetGold(uint32_t aActorFormId, uint32_t aGold) const noexcept
{
    if (!m_actorBridge.ActorExists(aActorFormId))
        return;
    Inventory::Entry change;
    change.BaseId = Payloads::PartyGold::kGold;
    change.Count = static_cast<int32_t>(static_cast<int64_t>(aGold) - GoldOf(aActorFormId));
    if (change.Count == 0)
        return;
    ScopedInventoryOverride _;
    m_formBridge.ObjectAddOrRemoveItem(aActorFormId, change);
}

void InventoryService::OnSpawnItemsApplied(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    if (!aRegistry.all_of<FormIdComponent>(aEntity))
        return;
    const uint32_t cFormId = aRegistry.get<FormIdComponent>(aEntity).Id;
    if (!m_actorBridge.IsOtherPlayer(cFormId))
        return;
    if (m_partyGold)
        SetGold(cFormId, *m_partyGold);
    if (m_partyItems)
        SetPartyItems(cFormId, *m_partyItems);
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

    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastNakedCheck < cDelayBetweenUpdates)
        return;

    m_lastNakedCheck = now;

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
