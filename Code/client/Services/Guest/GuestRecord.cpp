#include <Services/Guest/GuestRecord.h>

#include <Bridges/IJoinBridge.h>
#include <Events/AddExperienceEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/MenuEvent.h>
#include <Events/SessionLeavingEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/JoinMessages.h>
#include <Services/JoinService.h>
#include <Services/TransportService.h>
#include <World.h>

GuestRecord::GuestRecord(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_bridge(aBridge)
{
    Listen<InventoryChangeEvent, &GuestRecord::OnInventory>(m_connections, aDispatcher, this);
    Listen<EquipmentChangeEvent, &GuestRecord::OnEquipment>(m_connections, aDispatcher, this);
    Listen<AddExperienceEvent, &GuestRecord::OnExperience>(m_connections, aDispatcher, this);
    Listen<MenuEvent, &GuestRecord::OnMenu>(m_connections, aDispatcher, this);
    Listen<SessionLeavingEvent, &GuestRecord::OnLeaving>(m_connections, aDispatcher, this);
}

void GuestRecord::OnLeaving(const SessionLeavingEvent&) noexcept
{
    if (m_changed)
        Send();
}

// Perks, spells, training and books change through menus.
void GuestRecord::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (!acEvent.Opening)
        m_changed = true;
}

void GuestRecord::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    m_sinceSent += acEvent.Delta;
    if (m_changed && m_sinceSent >= kSeconds)
        Send();
}

bool GuestRecord::Send() noexcept
{
    PlayerRecordRequest request;
    request.Record = m_bridge.ReadPlayer();
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
