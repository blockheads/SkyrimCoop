#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

class IJoinBridge;
struct AddExperienceEvent;
struct EquipmentChangeEvent;
struct InventoryChangeEvent;
struct MenuEvent;
struct SessionLeavingEvent;
struct UpdateEvent;
struct World;

/**
 * @brief This guest's character for the host's save (docs/design/join-flow.md, "As built"): sent on joining, after it
 * changes (items, equipment, experience, a menu closing) at most once in kSeconds, and on leaving.
 */
struct GuestRecord
{
    static constexpr double kSeconds = 5.0;

    GuestRecord(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept;

    NO_COPY_MOVE(GuestRecord);

    // Passed on by GuestService.
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnInventory(const InventoryChangeEvent&) noexcept { m_changed = true; }
    void OnEquipment(const EquipmentChangeEvent&) noexcept { m_changed = true; }
    void OnExperience(const AddExperienceEvent&) noexcept { m_changed = true; }
    void OnMenu(const MenuEvent& acEvent) noexcept;
    void OnLeaving(const SessionLeavingEvent&) noexcept;
    bool Send() noexcept;

    World& m_world;
    IJoinBridge& m_bridge;
    bool m_changed{true};
    double m_sinceSent{kSeconds};
    std::vector<entt::scoped_connection> m_connections;
};
