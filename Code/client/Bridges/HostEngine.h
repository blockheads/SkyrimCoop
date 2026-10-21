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
    explicit HostEngine(const ModSystem& acMods) noexcept;

    // The weather the game shows; none for no sky, or the world map's own.
    [[nodiscard]] std::optional<GameId> ReadWeather() const noexcept;
    // Every global but the constants; the calendar's are marked.
    [[nodiscard]] TiltedPhoques::Vector<HostGlobal> ReadGlobals() const noexcept;
    // Every quest as the game holds it now.
    [[nodiscard]] TiltedPhoques::Vector<QuestState> ReadQuests() const noexcept;
    // Runs the clock on to that hour, the next day's if it is earlier, as a wait does.
    void SetHour(float aHours) noexcept;

private:
    const ModSystem& m_mods;
};
