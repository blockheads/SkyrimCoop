#pragma once

#include <Messages/CellStateRequest.h>
#include <Bridges/PhysicsBodies.h>
#include <Messages/WorldState.h>
#include <Services/Host/HostGame.h>
#include <Structs/GameId.h>

#include <map>
#include <optional>

struct TESObjectREFR;

namespace ReferenceBodies
{
struct Body;
}

/**
 * @brief The host's game as the host's rules read and change it (HostGame's engine side), in session ids.
 * Only while hosting: a guest's game never reads these. A form whose mod the session lacks is left out.
 */
class HostEngine
{
public:
    // Watches the engine's change funnel (FormChanges) for the references' state while it lives.
    HostEngine() noexcept;
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
    // A guest's player's hand on a reference's body (HostGame::PushBody, DriveBody, FreeBody): pushed alike, held
    // keyframed at its frame (its node with it), or simulated again from that frame as it was before.
    bool PushBody(const BodyPush& acPush) noexcept;
    bool DriveBody(const BodyFrame& acFrame) noexcept;
    bool FreeBody(const BodyFrame& acFrame) noexcept;
    [[nodiscard]] std::vector<BodyPose> ReadBodies(const std::vector<BodyKey>& acKeys) const noexcept;
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

    // A shared reference's body by its key; none for an actor's.
    [[nodiscard]] ReferenceBodies::Body FindObjectBody(const BodyKey& acKey) const noexcept;

    PhysicsBodies m_bodies;
    std::map<BodyKey, uint8_t> m_motions; // the bodies a guest's player holds: their own motion type
    std::vector<ObjectChange> m_events; // the scripts' events since the last TakeObjectChanges
    std::unordered_map<uint32_t, uint32_t> m_created; // the created references that went out: id -> base
};
