#include <Services/InventoryService.h>
#include <Games/Animation/GraphStream.h>
#include <Games/EquipmentRepair.h>
#include <Events/PlayerProgressEvent.h>
#include <Bridges/IPlayerBridge.h>

#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/Relayed.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/ItemsChangedEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Events/WornResetEvent.h>
#include <Services/CopyRecords.h>
#include <Structs/ItemsPart.h>

#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Games/Overrides.h>
#include <Games/PartyItems.h>
#include <set>
#include <Components/LocalComponent.h>
#include <Components/RemoteAnimationComponent.h>
#include <Components/RemoteComponent.h>
#include <SessionIds.h>
#include <Components/FormIdComponent.h>
#include <Components/WaitingFor3D.h>
#include <FormIds.h>
#include <Diagnostics/Profile.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                    IActorBridge& aActorBridge, IFormBridge& aFormBridge, CopyRecords& aRecords) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
    , m_records(aRecords)
{
    m_records.Register(
        RecordKind::kItems,
        [this](uint32_t aId, const std::string& acBytes) {
            ApplyItems(aId, acBytes);
            return std::vector<uint32_t>{};
        },
        [this](uint32_t aFormId) -> std::optional<std::string> { return ToPartBytes(ItemsPart::Of(ReadItems(aFormId))); });
    Listen<UpdateEvent, &InventoryService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<InventoryChangeEvent, &InventoryService::OnInventoryChangeEvent>(m_connections, m_dispatcher, this);
    Listen<EquipmentChangeEvent, &InventoryService::OnEquipmentChangeEvent>(m_connections, m_dispatcher, this);
    Listen<ItemsChangedEvent, &InventoryService::OnItemsChanged>(m_connections, m_dispatcher, this);
    Listen<ReferenceLoadedEvent, &InventoryService::OnReferenceLoaded>(m_connections, m_dispatcher, this);
    Listen<WornResetEvent, &InventoryService::OnWornReset>(m_connections, m_dispatcher, this);
    Listen<NotifyInventoryChanges, &InventoryService::OnNotifyInventoryChanges>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyGold, &InventoryService::OnNotifyPartyGold>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyItem, &InventoryService::OnNotifyPartyItem>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyItems, &InventoryService::OnNotifyPartyItems>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &InventoryService::OnDisconnected>(m_connections, m_dispatcher, this);
    Listen<&InventoryService::OnSpawnItemsApplied>(m_connections, m_world.on_destroy<WaitingFor3D>(), this);
}

InventoryService::~InventoryService() noexcept
{
    m_records.Register(RecordKind::kItems, {});
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    PROFILE_ZONE("InventoryService::OnUpdate");
    DeclareChanges();
    for (auto it = m_waiting3D.begin(); it != m_waiting3D.end();)
    {
        const uint32_t cId = *it;
        const uint32_t cFormId = Utils::GetFormIdBySessionId(cId);
        if (cFormId && m_actorBridge.IsActorCopy(cFormId) && !m_actorBridge.ActorHas3D(cFormId))
        {
            ++it;
            continue;
        }
        it = m_waiting3D.erase(it);
        if (cFormId)
            m_records.ApplyAgain(cId, RecordKind::kItems);
    }
    for (auto it = m_unsettled.begin(); it != m_unsettled.end();)
    {
        auto& [since, last] = it->second;
        since += acUpdateEvent.Delta;
        last += acUpdateEvent.Delta;
        const uint32_t cId = it->first;
        if (since >= kGiveUp)
        {
            spdlog::warn("InventoryService: copy {:X}'s worn set did not settle in {} s", cId, kGiveUp);
            it = m_unsettled.erase(it);
            continue;
        }
        ++it;
        if (last >= kRetry)
        {
            last = 0.0;
            m_records.ApplyAgain(cId, RecordKind::kItems); // may erase cId from m_unsettled: it is past already
        }
    }
    RunNakedNPCBugChecks();
}

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    Touch(acEvent.FormId);
    // An actor's items reach its copies as its part; a player character's change still goes to the host for the party's
    // gold and items (HostPurse, HostPartyItems: the host's scripts may change a guest's copy).
    const bool cPlayer = acEvent.FormId == m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId() || m_actorBridge.IsOtherPlayer(acEvent.FormId);
    if (!m_transport.IsConnected() || !cPlayer)
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
    const uint32_t cItemId = ToGame(acEvent.Item.BaseId);
    if (!acEvent.Drop && acEvent.Item.Count > 0 && !acEvent.Item.ExtraName.starts_with(PartyItems::cTag) &&
        PartyItems::ArrivesAsParty(cItemId, m_formBridge.GetFormType(cItemId), acEvent.Item.IsQuestItem, acEvent.StoryGiven))
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
        Touch(cActorFormId);
    }
    else
    {
        const uint32_t cObjectFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
        if (cObjectFormId == 0 || !m_formBridge.FormExists(cObjectFormId))
            return;

        ScopedInventoryOverride _;

        m_formBridge.ObjectAddOrRemoveItem(cObjectFormId, acMessage.Item);
        Touch(cObjectFormId); // silent above: this game's own actor's items part goes out
        if (cObjectFormId == m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId())
            m_dispatcher.trigger(PlayerProgressEvent{});
    }
}

bool InventoryService::ApplyWorn(uint32_t aFormId, const Inventory& acWanted) noexcept
{
    const uint32_t cActorFormId = aFormId;

    // Worn items as (game form id, left hand) on each side.
    const auto worn = [&](const Inventory& acInventory) {
        std::set<std::pair<uint32_t, bool>> items;
        for (const auto& entry : acInventory.Entries)
            if (entry.IsWorn())
                if (const uint32_t cId = ToGame(entry.BaseId))
                    items.insert({cId, entry.ExtraWornLeft});
        return items;
    };
    const auto cWanted = worn(acWanted);
    const auto cEquipment = m_formBridge.GetActorEquipment(cActorFormId);
    // A copy's move to another cell drops its hand spells (docs/re/systems/copy-hand-spells.md).
    const auto& cWantedMagic = acWanted.CurrentMagicEquipment;
    // A spell can stay selected while the hand holds nothing: a loaded copy's hands are read too.
    const bool cLoaded = m_actorBridge.ActorHas3D(cActorFormId);
    const auto holds = [&](const GameId& acSpell, uint32_t aSlot) {
        return !cLoaded || EquipmentRepair::HandHolds(ToGame(acSpell), m_actorBridge.GetEquippedWeaponFormId(cActorFormId, aSlot));
    };
    // A worn weapon or torch in each hand it is held in (a two-hander both): 418622 empties the hands and leaves worn
    // flags. docs/re/systems/hand-slots.md.
    const auto cHandsHoldWorn = [&] {
        if (!cLoaded)
            return true;
        // The left hand held by something else worn: a torch, a left weapon or a left-hand spell.
        bool leftTaken = static_cast<bool>(cWantedMagic.LeftHandSpell);
        for (const auto& [cId, cLeft] : cWanted)
            if (!m_formBridge.IsTwoHandedWeapon(cId) &&
                EquipmentRepair::HandsOf(m_formBridge.GetFormType(cId), cLeft) == EquipmentRepair::kLeftHandBit)
                leftTaken = true;
        for (const auto& [cId, cLeft] : cWanted)
        {
            const uint8_t cHands = EquipmentRepair::HandsOf(m_formBridge.GetFormType(cId), cLeft, m_formBridge.IsTwoHandedWeapon(cId), leftTaken);
            for (uint32_t hand = 0; hand < 2; ++hand)
                if ((cHands & (1u << hand)) && m_actorBridge.GetEquippedWeaponFormId(cActorFormId, hand) != cId)
                    return false;
        }
        return true;
    };
    const auto cHandsAgree = [&](const Inventory& acNow) {
        return cWantedMagic.LeftHandSpell == acNow.CurrentMagicEquipment.LeftHandSpell &&
               cWantedMagic.RightHandSpell == acNow.CurrentMagicEquipment.RightHandSpell &&
               holds(cWantedMagic.LeftHandSpell, 0) && holds(cWantedMagic.RightHandSpell, 1) && cHandsHoldWorn();
    };
    const bool cSameHands = cHandsAgree(cEquipment);
    if (cLoaded && !cHandsHoldWorn())
        spdlog::info("Equipment reconcile {:X}: hands [{:X}, {:X}] hold less than its worn set", cActorFormId,
                     m_actorBridge.GetEquippedWeaponFormId(cActorFormId, 0), m_actorBridge.GetEquippedWeaponFormId(cActorFormId, 1));

    EquipmentRepair::Apply(cWanted, [&] { return worn(m_formBridge.GetActorEquipment(cActorFormId)); }, cSameHands,
        [&] {
            spdlog::info("Equipment reconcile {:X}: reset worn set", cActorFormId);
            m_formBridge.ActorResetEquipment(cActorFormId, acWanted.CurrentMagicEquipment);
        },
        [&](uint32_t aItemId, bool aLeft) {
            if (!m_formBridge.FormExists(aItemId))
                return;
            for (const auto& cEntry : acWanted.Entries)
                if (ToGame(cEntry.BaseId) == aItemId)
                {
                    CarryForEquip(cActorFormId, cEntry.BaseId);
                    break;
                }
            m_formBridge.ActorEquipItem(cActorFormId, aItemId,
                                        EquipmentRepair::EquipSlot(m_formBridge.GetFormType(aItemId), aLeft, m_formBridge.IsTwoHandedWeapon(aItemId)), 1);
        },
        [&](uint32_t aItemId, bool aLeft) {
            if (m_formBridge.FormExists(aItemId))
                m_formBridge.ActorUnequipItem(cActorFormId, aItemId,
                                              EquipmentRepair::EquipSlot(m_formBridge.GetFormType(aItemId), aLeft, m_formBridge.IsTwoHandedWeapon(aItemId)), 1);
        });
    // The selected shout or power (EquipShout): no worn flag, apart from the worn set.
    const GameId cShout = m_formBridge.GetActorEquipment(cActorFormId).CurrentMagicEquipment.Shout;
    if (cShout != cWantedMagic.Shout)
    {
        if (cWantedMagic.Shout)
            m_formBridge.ActorEquipShout(cActorFormId, ToGame(cWantedMagic.Shout));
        else
            m_formBridge.ActorUnequipShout(cActorFormId, ToGame(cShout));
    }
    const Inventory cAfter = m_formBridge.GetActorEquipment(cActorFormId);
    return worn(cAfter) == cWanted && cHandsAgree(cAfter) && cAfter.CurrentMagicEquipment.Shout == cWantedMagic.Shout;
}

Inventory InventoryService::ReadItems(uint32_t aFormId) const noexcept
{
    constexpr uint8_t cLeveledItem = 53; // FormType::LeveledItem: a base container's leveled list, listed as it is
    Inventory items = m_actorBridge.GetActorInventory(aFormId);
    const bool cPlayer = aFormId == m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId() || m_actorBridge.IsOtherPlayer(aFormId);
    // A player's gold is the party's purse; its party stacks stay in (their worn state), counted by the party store.
    std::erase_if(items.Entries, [&](const Inventory::Entry& acEntry) {
        return m_formBridge.GetFormType(ToGame(acEntry.BaseId)) == cLeveledItem ||
               (cPlayer && acEntry.BaseId == Payloads::PartyGold::kGold);
    });
    return items;
}

void InventoryService::Touch(uint32_t aFormId) noexcept
{
    if (aFormId && m_transport.IsConnected() && !m_actorBridge.IsActorCopy(aFormId))
        m_touched.insert(aFormId);
}

void InventoryService::OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept
{
    Touch(acEvent.ActorId);
}

void InventoryService::OnItemsChanged(const ItemsChangedEvent& acEvent) noexcept
{
    Touch(acEvent.FormId);
}

void InventoryService::OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept
{
    // A 3D load: hand spells are not equipped again (Actor::Set3D 37178, copy-hand-spells.md), and a living actor's
    // worn set is reset to its base's outfit (TESNPC::InitWornOn3D 24725).
    Refresh(acEvent.FormId);
}

void InventoryService::OnWornReset(const WornResetEvent& acEvent) noexcept
{
    Refresh(acEvent.FormId);
}

void InventoryService::Refresh(uint32_t aFormId) noexcept
{
    if (!m_actorBridge.IsActorCopy(aFormId))
        return Touch(aFormId);
    if (const auto cId = Utils::GetSessionIdByFormId(aFormId))
        m_records.ApplyAgain(*cId, RecordKind::kItems);
}

void InventoryService::DeclareChanges() noexcept
{
    if (m_touched.empty())
        return;
    const auto cTouched = std::exchange(m_touched, {});
    for (const auto cEntity : m_world.view<FormIdComponent, LocalComponent>())
    {
        const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
        if (!cTouched.contains(cFormId) || !m_actorBridge.ActorExists(cFormId))
            continue;
        const uint32_t cId = m_world.get<LocalComponent>(cEntity).Id;
        const Inventory cNow = ReadItems(cFormId);
        ItemsPart held;
        const RecordPart* cpHeld = m_records.Find(cId, RecordKind::kItems);
        if (!cpHeld || !FromPartBytes(cpHeld->Bytes, held))
        {
            m_records.Declare(cId, RecordKind::kItems, ToPartBytes(ItemsPart::Of(cNow)));
            continue;
        }
        if (const auto cChange = ItemsPart::Change(held, cNow))
        {
            spdlog::debug("Items of {:X}: {} stacks changed", cFormId, cChange->Items.Entries.size());
            m_records.Declare(cId, RecordKind::kItems, ToPartBytes(*cChange));
        }
    }
}

void InventoryService::ApplyItems(uint32_t aId, const std::string& acBytes) noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aId);
    ItemsPart part;
    // Not a copy here, or its 3D not in yet: applied once it is (m_waiting3D; a new copy's at its first 3D, CharacterService).
    if (!cFormId || !m_actorBridge.IsActorCopy(cFormId) || !FromPartBytes(acBytes, part))
        return;
    if (!m_actorBridge.ActorHas3D(cFormId))
    {
        m_waiting3D.insert(aId);
        return;
    }
    const Inventory cHeld = ReadItems(cFormId);
    // A player's party stacks are the party store's (party-items.md): its copy keeps the ones it holds.
    const bool cPlayer = m_actorBridge.IsOtherPlayer(cFormId);
    // Each item's count and its stacks with extra data, worn flags aside: the wearing is ApplyWorn's.
    std::vector<GameId> items;
    for (const std::vector<Inventory::Entry>* cpStacks : {&cHeld.Entries, static_cast<const std::vector<Inventory::Entry>*>(&part.Items.Entries)})
        for (const auto& cStack : *cpStacks)
            if (std::ranges::find(items, cStack.BaseId) == items.end())
                items.push_back(cStack.BaseId);
    const auto cOf = [cPlayer](const std::vector<Inventory::Entry>& acStacks, const GameId& acItem, bool aParty = false) {
        std::pair<int32_t, std::vector<Inventory::Entry>> of;
        for (Inventory::Entry stack : acStacks)
        {
            if (stack.BaseId != acItem || (cPlayer && PartyItems::IsParty(stack) != aParty))
                continue;
            of.first += stack.Count;
            stack.ExtraWorn = stack.ExtraWornLeft = stack.IsQuestItem = false;
            if (!stack.ContainsExtraData())
                continue;
            const auto it = std::ranges::find_if(of.second, [&](const auto& acSame) { return ItemsPart::SameStack(acSame, stack); });
            if (it != of.second.end())
                it->Count += stack.Count;
            else
                of.second.push_back(stack);
        }
        return of;
    };
    const auto cSameStacks = [](std::vector<Inventory::Entry> aHave, const std::vector<Inventory::Entry>& acWanted) {
        for (const auto& cWanted : acWanted)
        {
            const auto it = std::ranges::find_if(aHave, [&](const auto& acHave) { return acHave.Count == cWanted.Count && ItemsPart::SameStack(acHave, cWanted); });
            if (it == aHave.end())
                return false;
            aHave.erase(it);
        }
        return aHave.empty();
    };
    for (const GameId& cItem : items)
    {
        const auto [cWantedCount, cWantedStacks] = cOf(part.Items.Entries, cItem);
        const auto [cHeldCount, cHeldStacks] = cOf(cHeld.Entries, cItem);
        if (cWantedCount == cHeldCount && cSameStacks(cHeldStacks, cWantedStacks))
            continue;
        spdlog::debug("Copy {:X}: item {:X} {} -> {}", cFormId, cItem.LogFormat(), cHeldCount, cWantedCount);
        auto [count, stacks] = cOf(part.Items.Entries, cItem);
        if (cPlayer)
        {
            const auto [cPartyCount, cPartyStacks] = cOf(cHeld.Entries, cItem, true);
            count += cPartyCount;
            stacks.insert(stacks.end(), cPartyStacks.begin(), cPartyStacks.end());
        }
        m_formBridge.SetCopyItemCount(cFormId, cItem, count, stacks);
    }
    if (ApplyWorn(cFormId, part.Items))
        m_unsettled.erase(aId);
    else
        m_unsettled.try_emplace(aId, 0.0, 0.0);
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

        // A copy wears its owner's (its items part); ResetInventory does nothing to one (HookResetInventory).
        if (m_actorBridge.IsOtherPlayer(formId) || m_actorBridge.IsActorCopy(formId))
            continue;

        if (m_actorBridge.IsActorDead(formId))
            continue;

        if (m_formBridge.ActorIsWearingBodyPiece(formId))
            continue;

        if (!m_formBridge.ActorShouldWearBodyPiece(formId))
            continue;

        spdlog::info("InventoryService: {:X} wears no body piece it should: its inventory reset", formId);
        // Don't broadcast changes, it'll just make things messier.
        ScopedEquipOverride seo;
        ScopedInventoryOverride sio;

        m_formBridge.ActorResetInventory(formId, false);
        Touch(formId); // silent above: its items part goes out
    }
}
