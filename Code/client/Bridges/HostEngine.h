#pragma once

#include <Messages/CellStateRequest.h>
#include <Bridges/PhysicsRests.h>
#include <Messages/WorldState.h>
#include <Structs/GameId.h>

#include <optional>

struct ModSystem;
struct TESObjectREFR;

/**
 * @brief The host's game as the host's rules read and change it (HostGame's engine side), in session ids.
 * Only while hosting: a guest's game never reads these. A form whose mod the session lacks is left out.
 */
class HostEngine
{
public:
    // Watches the engine's change funnel (FormChanges) for the references' state while it lives.
    explicit HostEngine(const ModSystem& acMods) noexcept;
    ~HostEngine() noexcept;

    HostEngine(const HostEngine&) = delete;
    HostEngine& operator=(const HostEngine&) = delete;

    // The weather the game shows; none for no sky, or the world map's own.
    [[nodiscard]] std::optional<GameId> ReadWeather() const noexcept;
    // Every global but the constants; the calendar's are marked.
    [[nodiscard]] TiltedPhoques::Vector<HostGlobal> ReadGlobals() const noexcept;
    // Every quest as the game holds it now.
    [[nodiscard]] TiltedPhoques::Vector<QuestState> ReadQuests() const noexcept;
    // Runs the clock on to that hour, the next day's if it is earlier, as a wait does.
    void SetHour(float aHours) noexcept;
    // A world script's event on an object, kept for the others: it activated a door (by a reference) or played an
    // animation on it; or a container's items changed (not an actor's).
    void RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept;
    void RecordAnimation(uint32_t aObjectFormId, const TiltedPhoques::String& acAnimation, const TiltedPhoques::String& acEvent) noexcept;
    void RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept;
    // A guest's lock change, accepted: the host's object takes it, and the others get it as the host's change.
    void SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept;
    // A guest's container change, accepted: the host's container holds that many, and the others get it.
    void SetItems(const GameId& acObject, const GameId& acItem, uint32_t aCount) noexcept;
    // Since the last call: each reference the game created, the state of each one the funnel saw change, the
    // physics objects that came to rest, the records that lost rows, then the scripts' events in order.
    [[nodiscard]] TiltedPhoques::Vector<ObjectChange> TakeObjectChanges() noexcept;
    // Every row of each reference in the cell that this game, or a guest's (acChanged), holds changes for, then each
    // container's contents after this game rolled it; none while the cell is not attached here.
    [[nodiscard]] std::optional<TiltedPhoques::Vector<ObjectChange>> ReadCellState(const GameId& acCell,
                                                                                  const TiltedPhoques::Vector<ChangedReference>& acChanged) const noexcept;

private:
    // The guests have it: a plugin's reference, or one this game created that went out (kCreate).
    [[nodiscard]] bool IsShared(uint32_t aFormId) const noexcept;

    const ModSystem& m_mods;
    PhysicsRests m_rests;
    TiltedPhoques::Vector<ObjectChange> m_events; // the scripts' events since the last TakeObjectChanges
    TiltedPhoques::Set<uint32_t> m_created;       // the references the game created that went out, alive
};
