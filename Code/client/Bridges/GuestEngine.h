#pragma once

#include <Messages/WorldState.h>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

struct ModSystem;

/**
 * @brief A guest's game taking the host's world, each with the engine's own call, from session ids.
 * Only while a guest: the host's world is its own.
 */
class GuestEngine
{
public:
    explicit GuestEngine(const ModSystem& acMods) noexcept;

    // The calendar as the engine's clock leaves it (Calendar::Update 36291), date and days passed included.
    void SetCalendar(const TimeModel& acTime) noexcept;
    // Shows the weather, again after the game changed its own; leaves the world map's alone. False: no such weather.
    bool HoldWeather(const GameId& acWeather) noexcept;
    // The game's own weather again.
    void ReleaseWeather() noexcept;
    // Sets it as Papyrus' GlobalVariable.SetValue (55923) does; false for none or a constant.
    bool SetGlobal(const GlobalValue& acGlobal) noexcept;
    // Makes the quest so, each stage without its fragment (docs/re/systems/quest-apply.md); aAsLoad: as a save
    // load does, the fields only. Fills of references the host's game created are left as they are.
    void ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept;
    // On or off: the game's own story manager starting quests for story events (arrivals, kills, scripts' events).
    void SetOwnQuestStarts(bool aOn) noexcept;

private:
    const ModSystem& m_mods;
};
