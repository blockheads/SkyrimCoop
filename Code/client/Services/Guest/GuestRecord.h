#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>

class IJoinBridge;
struct PlayerProgressEvent;
struct PlayerRenamedEvent;
struct EquipmentChangeEvent;
struct HealthChangeEvent;
struct SpellCastEvent;
struct InventoryChangeEvent;
struct MenuEvent;
struct SessionLeavingEvent;
struct UpdateEvent;
struct World;

/**
 * @brief This guest's character for the host's save (docs/design/join-flow.md, "As built"): sent on joining, after it
 * changes (items, the party's gold, equipment, experience, the party's progression, a menu closing, its health, a cast
 * of its own) at most once in kSeconds, and on leaving.
 */
struct GuestRecord
{
    static constexpr double kSeconds = 5.0;
    static constexpr uint32_t kPlayerForm = 0x14; // this game's player (PlayerCharacter)

    GuestRecord(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept;

    NO_COPY_MOVE(GuestRecord);

    // Passed on by GuestService.
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnInventory(const InventoryChangeEvent&) noexcept { m_changed = true; }
    void OnEquipment(const EquipmentChangeEvent&) noexcept { m_changed = true; }
    void OnProgress(const PlayerProgressEvent&) noexcept { m_changed = true; }
    void OnPartyGold(const NotifyPartyGold&) noexcept { m_changed = true; }
    // Its damage and timed effects, power cooldowns: what its save keeps beside the character (player-save-state.md).
    void OnHealth(const HealthChangeEvent& acEvent) noexcept;
    void OnCast(const SpellCastEvent& acEvent) noexcept;
    void OnMenu(const MenuEvent& acEvent) noexcept;
    // A new name changes who the character is: the host's next save must have it at once (names.md).
    void OnRenamed(const PlayerRenamedEvent&) noexcept;
    void OnLeaving(const SessionLeavingEvent&) noexcept;
    bool Send() noexcept;

    World& m_world;
    IJoinBridge& m_bridge;
    bool m_changed{true};
    double m_sinceSent{kSeconds};
    std::vector<entt::scoped_connection> m_connections;
};
