#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <MainLoopTasks.h>
#include <ObjectAnimations.h>
#include <Messages/RelayedMessages.h>
#include <Messages/WorldState.h>
#include <Structs/ScriptValue.h>

#include <chrono>

struct NotifyGlobals;
struct NotifyQuests;
struct NotifyObjectChanges;
struct LockChangeEvent;
struct ContainerChangeEvent;
struct ItemDroppedEvent;
struct ItemPickUpEvent;
struct SummonEvent;

struct OwnStoryEvent;
struct NotifyItemDropped;
struct NotifyPickUp;
struct UpdateEvent;
struct World;
class GuestEngine;

/**
 * @brief A guest takes the host's globals and quests (HostWorldState) with its engine's own calls
 * (docs/design/world-state-sync.md step 4). Its own story manager starts no quest meanwhile, and its world
 * scripts do not react to its player: the host's world scripts' doors and animations come from the host (step 5).
 * Its own lock and container changes are proposals the host's game takes (step 6). Each cell it loads asks for the
 * host's state there, what it missed while away included. Its story events are the host's story manager's. Its
 * player's drops are made in the host's world, and its pickups of the party's loose items are the host's to grant
 * (docs/design/dropped-items.md).
 */
struct GuestWorldState
{
    GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestWorldState() noexcept;

    NO_COPY_MOVE(GuestWorldState);

    // Each frame, from GuestService: each cell newly attached here (a load or back from the cell buffer, which raises
    // no load event) asks for the host's state there.
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnGlobals(const NotifyGlobals& acMessage) noexcept;
    void OnQuests(const NotifyQuests& acMessage) noexcept;
    void OnObjectChanges(const NotifyObjectChanges& acMessage) noexcept;
    // This game's lock change (its player picked it, or a key): proposed to the host, whose world decides.
    void OnLockChange(const LockChangeEvent& acEvent) noexcept;
    // This game's container changed (its player took or stored an item): its count now proposed to the host.
    void OnContainerChange(const ContainerChangeEvent& acEvent) noexcept;
    void AskCellState(uint32_t aCellFormId) noexcept;
    // This game raised a story event: it goes to the host's story manager (HostStory), this one's starts no quest.
    void OnStoryEvent(const OwnStoryEvent& acEvent) noexcept;
    // This game's player dropped an item: the host's game makes the party's, this one becomes its twin.
    void OnItemDropped(const ItemDroppedEvent& acEvent) noexcept;
    void OnItemDroppedNotify(const NotifyItemDropped& acMessage) noexcept;
    // This game's player reached for the party's loose item: the host's game grants it to the first to ask.
    void OnItemPickUp(const ItemPickUpEvent& acEvent) noexcept;
    void OnPickUpNotify(const NotifyPickUp& acMessage) noexcept;
    // This game's player cast a summon: the host's game makes it on this player's copy (summons.md).
    void OnSummon(const SummonEvent& acEvent) noexcept;
    // The host's game answered this game's script waiting on it (a created reference): it goes on with the host's
    // reference here once that is here (R5), or None after kCreatedWait.
    void OnCallResult(const NotifyPlayerNativeResult& acMessage) noexcept;
    // Each waiting answer whose reference is here now; true: answered.
    bool AnswerCall(uint32_t aReplyId, const ScriptValue& acResult) noexcept;

    World& m_world;
    GuestEngine& m_engine;
    // The attached cells already asked for.
    std::vector<uint32_t> m_cells;
    struct WaitingAnswer
    {
        uint32_t ReplyId;
        ScriptValue Result;
        std::chrono::steady_clock::time_point Since;
    };
    ObjectAnimations<uint64_t, ObjectChange> m_animations; // the host's scripts', each once its graph takes it
    std::vector<WaitingAnswer> m_answers; // the host's answers naming a reference not here yet

    MainLoopTasks::Scope m_mainTasks;
    std::vector<entt::scoped_connection> m_connections;
};
