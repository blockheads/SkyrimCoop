#include <join/GuestRecord.h>

#include <join/IJoinBridge.h>
#include <core/PlayerProgressEvent.h>
#include <core/PlayerRenamedEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <magic/SpellCastEvent.h>
#include <items/InventoryChangeEvent.h>
#include <core/MenuEvent.h>
#include <core/SessionLeavingEvent.h>
#include <core/UpdateEvent.h>
#include <Messages/JoinMessages.h>
#include <Messages/Relayed.h>
#include <join/JoinService.h>
#include <core/TransportService.h>
#include <core/World.h>
#include <core/Profile.h>

GuestRecord::GuestRecord(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_bridge(aBridge)
{
    Listen<InventoryChangeEvent, &GuestRecord::OnInventory>(m_connections, aDispatcher, this);
    Listen<EquipmentChangeEvent, &GuestRecord::OnEquipment>(m_connections, aDispatcher, this);
    Listen<PlayerProgressEvent, &GuestRecord::OnProgress>(m_connections, aDispatcher, this);
    Listen<NotifyPartyGold, &GuestRecord::OnPartyGold>(m_connections, aDispatcher, this);
    Listen<MenuEvent, &GuestRecord::OnMenu>(m_connections, aDispatcher, this);
    Listen<HealthChangeEvent, &GuestRecord::OnHealth>(m_connections, aDispatcher, this);
    Listen<SpellCastEvent, &GuestRecord::OnCast>(m_connections, aDispatcher, this);
    Listen<PlayerRenamedEvent, &GuestRecord::OnRenamed>(m_connections, aDispatcher, this);
    Listen<SessionLeavingEvent, &GuestRecord::OnLeaving>(m_connections, aDispatcher, this);
}

void GuestRecord::OnLeaving(const SessionLeavingEvent&) noexcept
{
    if (m_changed && !m_bridge.IsAtMainMenu())
        Send();
}

void GuestRecord::OnHealth(const HealthChangeEvent& acEvent) noexcept
{
    m_changed |= acEvent.HitteeId == kPlayerForm;
}

void GuestRecord::OnCast(const SpellCastEvent& acEvent) noexcept
{
    m_changed |= acEvent.CasterFormId == kPlayerForm;
}

// Perks, spells, training and books change through menus.
void GuestRecord::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (acEvent.Opening)
        return;
    m_changed = true;
    // The race menu changes who the character is: the host's next save must have it (docs/re/systems/names.md).
    if (acEvent.Menu == "RaceSex Menu")
        m_sinceSent = kSeconds;
}

void GuestRecord::OnRenamed(const PlayerRenamedEvent&) noexcept
{
    m_changed = true;
    m_sinceSent = kSeconds;
}

void GuestRecord::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("GuestRecord::OnUpdate");
    m_sinceSent += acEvent.Delta;
    if (m_changed && m_sinceSent >= kSeconds)
        Send();
}

bool GuestRecord::Send() noexcept
{
    PlayerRecordRequest request;
    request.Record = m_bridge.ReadPlayer();
    request.Record.Knowledge = m_bridge.ReadKnowledge();
    request.Record.Key = m_world.ctx().at<JoinService>().PlayerKey();
    if (!m_world.GetTransport().Send(request))
        return false;
    spdlog::debug("GuestRecord: sent {} ({}, level {}, {} items, {} carried)", request.Record.Key, request.Record.Name, request.Record.Level,
                  request.Record.Items.Entries.size(),
                  std::ranges::fold_left(request.Record.Items.Entries, 0, [](int aSum, const auto& acEntry) { return aSum + acEntry.Count; }));
    m_changed = false;
    m_sinceSent = 0.0;
    return true;
}
