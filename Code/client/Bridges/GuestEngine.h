#pragma once

#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

#include <optional>

struct ModSystem;

/**
 * @brief A guest's game taking the host's world, each with the engine's own call, from session ids.
 * Only while a guest: the host's world is its own.
 */
class GuestEngine
{
public:
    explicit GuestEngine(const ModSystem& acMods) noexcept;
    ~GuestEngine() noexcept;

    TP_NOCOPYMOVE(GuestEngine);

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
    // Off while a guest, the host's game deciding instead: its story manager starts no quest, and its world's
    // scripts do not react to its player (activations, triggers).
    void SetDecidesWorld(bool aOn) noexcept;
    // Makes the host's world object change here: activates the door (if it is as the host's was), plays the
    // animation, enables or disables the reference, sets its lock, makes the container hold that many. False: the
    // object, the activator or the item is not in this game.
    // aLoad: a cell's state, not a change; a state already as the host's is left alone (no events for it).
    bool ApplyObjectChange(const ObjectChange& acChange, bool aLoad = false) noexcept;
    // This game's container's count of the item now (Bridges/ContainerItems.h), for a proposal to the host.
    [[nodiscard]] std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept;
    // The cells loaded here now (the interior, or the exterior grid's attached ones), by form id.
    [[nodiscard]] TiltedPhoques::Vector<uint32_t> AttachedCells() const noexcept;
    // Asks for the host's state of that loaded cell, naming the references this game holds changes for; none for a
    // cell no longer loaded or not in the session's mods.
    [[nodiscard]] std::optional<CellStateRequest> ReadCellChanges(uint32_t aCellFormId) const noexcept;
    [[nodiscard]] bool IsCellAttached(const GameId& acCell) const noexcept;

private:
    const ModSystem& m_mods;
};
