#pragma once

#include <Messages/WorldState.h>
#include <Structs/GameId.h>

#include <optional>

struct ModSystem;

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
    // animation on it.
    void RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept;
    void RecordAnimation(uint32_t aObjectFormId, const TiltedPhoques::String& acAnimation, const TiltedPhoques::String& acEvent) noexcept;
    // A guest's lock change, accepted: the host's object takes it, and the others get it as the host's change.
    void SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept;
    // Since the last call: the state of each reference the funnel saw change (enabled, lock), then the events
    // in order.
    [[nodiscard]] TiltedPhoques::Vector<ObjectChange> TakeObjectChanges() noexcept;

private:
    const ModSystem& m_mods;
    TiltedPhoques::Vector<ObjectChange> m_events; // the scripts' events since the last TakeObjectChanges
};
