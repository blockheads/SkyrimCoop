#pragma once

#include <Messages/CellStateRequest.h>
#include <Bridges/PhysicsBodies.h>
#include <Messages/WorldState.h>
#include <Services/Host/HostGame.h>
#include <Structs/GameId.h>

#include <map>
#include <optional>

struct TESObjectREFR;

/**
 * @brief The host's game as the host's rules read and change it (HostGame's engine side), in session ids.
 * Only while hosting: a guest's game never reads these. A form whose mod the session lacks is left out.
 */
class HostEngine
{
public:
    // Watches the engine's change funnel (FormChanges) for the references' state while it lives; aBodies finds the
    // references the guests have through it.
    explicit HostEngine(class IBodyBridge& aBodies) noexcept;
    ~HostEngine() noexcept;

    HostEngine(const HostEngine&) = delete;
    HostEngine& operator=(const HostEngine&) = delete;

    // The weather the game shows; none for no sky, or the world map's own.
    [[nodiscard]] std::optional<GameId> ReadWeather() const noexcept;
    // Every global but the constants; the calendar's are marked.
    [[nodiscard]] std::vector<HostGlobal> ReadGlobals() const noexcept;
    // Only globals written since the last call; constants excluded, calendar marked.
    [[nodiscard]] std::vector<HostGlobal> TakeGlobalChanges() const noexcept;
    // Every quest as the game holds it now.
    [[nodiscard]] std::vector<QuestState> ReadQuests() const noexcept;
    // Only quests whose state or alias references changed since the last call.
    [[nodiscard]] std::vector<QuestState> TakeQuestChanges() const noexcept;
    // Runs the clock on to that hour, the next day's if it is earlier, as a wait does.
    void SetHour(float aHours) noexcept;
    // A world script's event on an object, kept for the others: it activated a door (by a reference) or played an
    // animation on it; or a container's items changed (not an actor's).
    void RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept;
    void RecordAnimation(uint32_t aObjectFormId, const std::string& acAnimation, const std::string& acEvent) noexcept;
    void RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept;
    // A guest's lock change, accepted: the host's object takes it, and the others get it as the host's change.
    void SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept;
    // A guest's container change, accepted: the host's container holds that many, and the others get it.
    void SetItems(const GameId& acObject, const GameId& acItem, uint32_t aCount) noexcept;
    // Since the last call: each reference the game created, the state of each one the funnel saw change, the
    // physics objects that came to rest, the records that lost rows, then the scripts' events in order.
    [[nodiscard]] std::vector<ObjectChange> TakeObjectChanges() noexcept;
    // The physics bodies physics moves: the awake ones read now, and those that fell asleep (HostGame::TakeBodies); an
    // actor's ragdoll by its session id (aSessionOf: 0 for an actor the guests do not have).
    [[nodiscard]] HostBodies TakeBodies(const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept;
    // Every row of each reference in the cell that this game, or a guest's (acChanged), holds changes for, then each
    // container's contents after this game rolled it; none while the cell is not attached here.
    [[nodiscard]] std::optional<std::vector<ObjectChange>> ReadCellState(const GameId& acCell,
                                                                                  const std::vector<ChangedReference>& acChanged) const noexcept;

private:
    // The guests have it: a plugin's reference, or one this game created that went out (kCreate).
    [[nodiscard]] bool IsShared(uint32_t aFormId) const noexcept;
    // The created references that went out and are gone here (deleted, freed, or their id now another's), as
    // deletions for the guests: a script's Delete() of one often records no change (it is never saved).
    void TakeDeletedCreated(std::vector<ObjectChange>& aChanges) noexcept;

    IBodyBridge& m_bodyBridge;
    PhysicsBodies m_bodies;
    std::vector<ObjectChange> m_events; // the scripts' events since the last TakeObjectChanges
    std::unordered_map<uint32_t, uint32_t> m_created; // the created references that went out: id -> base
};
