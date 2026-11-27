#pragma once

#include <core/Listen.h>
#include <core/MainLoopTasks.h>
#include <Messages/RelayedMessages.h>
#include <physics/BodyPlayback.h>
#include <physics/RestPlacer.h>

#include <NoCopyMove.h>

#include <set>
#include <unordered_map>
#include <vector>

struct World;
struct UpdateEvent;
struct DisconnectedEvent;
class IBodyBridge;

/**
 * @brief Every game shows the bodies it does not simulate (docs/design/physics-authority-plan.md, piece 4):
 *
 *   moving   played kDelayMs behind the frame clock (BodyPlayback): steered onto each state; held in another game:
 *            keyframed (this game's grab refuses it). A copy's ragdoll plays at its copy's time
 *   at rest  put there by RestPlacer's steps (glide, a step awake, its island asleep), once; woken off it by this game's
 *            own physics, again twice at most
 *
 * A guest's Keeper (GuestBodies) has authority over some bodies: their states are not shown here.
 */
struct BodyService
{
    // A body's states, behind the sender's time: a state due now has arrived (half a round trip and a step's jitter).
    static constexpr uint64_t kDelayMs = 100;

    // This game's authority over the session's bodies (a guest's: GuestBodies).
    struct Keeper
    {
        virtual ~Keeper() = default;
        // Each update, before states are shown: the bodies this game took (their playback ends here).
        [[nodiscard]] virtual std::vector<BodyKey> Update(uint64_t aTick) noexcept = 0;
        // The host's state of a body: false while this game simulates it (not shown).
        [[nodiscard]] virtual bool Hears(const BodyState& acState) noexcept = 0;
        // This game has authority over that body now: no other game's rest of it is put here (a ragdoll's record part
        // the host declared from playing this game's states, an echo).
        [[nodiscard]] virtual bool Simulates(const BodyKey& acKey) const noexcept = 0;
    };

    BodyService(World& aWorld, entt::dispatcher& aDispatcher, IBodyBridge& aBridge) noexcept;

    NO_COPY_MOVE(BodyService);

    void SetKeeper(Keeper* apKeeper) noexcept { m_pKeeper = apKeeper; }
    // This game's actors' ragdolls came to rest (its own physics read them, keyed by session id): each one's rest is its
    // record part (RecordKind::kRagdoll), which every other game gets and every later spawn of it carries; one awake
    // again (acAwake) clears it. Rests taken out of aRests.
    void DeclareRagdolls(uint64_t aTick, std::vector<BodyPose>& aRests, const std::vector<BodyFrame>& acAwake) noexcept;
    // This game simulates the body now (it took it): its playback ends, its older states too.
    void Forget(const BodyKey& acKey, uint64_t aUntil = 0) noexcept;
    // This game gives the body back: the host's states of it stand, from before the taking too.
    void Reopen(const BodyKey& acKey) noexcept { PlaybackOf(acKey).Reopen(acKey); }
    // A reference another game's player holds (its states come held): its holder's alone, so this game's player
    // may not activate it (pick it up) meanwhile.
    [[nodiscard]] bool HeldElsewhere(uint32_t aFormId) const noexcept { return aFormId && m_heldElsewhere.contains(aFormId); }
    // An owner leaves its ragdoll on the delayed movement timeline: stop steering its resting bones.
    void EndRagdoll(uint32_t aSessionId, uint64_t aTick) noexcept;
    // A copy goes down with its owner's ragdoll action, before its owner's first body frame.
    void KnockDown(uint32_t aCopyFormId) noexcept;

private:
    // Another game's rest on this game's body, put there by its steps.
    struct Resting
    {
        BodyPose Pose{};
        uint64_t Tick{}; // its state's time: an older moving state does not end it
        RestPlacer Placer{};
        bool Placed{};     // the host's game put it there: held there asleep, never woken (HoldAt)
        uint32_t Frames{}; // the states played since the last rest (logged)
        bool Logged{};
    };

    void OnStates(const NotifyBodyStates& acMessage) noexcept;
    void Hold(const BodyKey& acKey, bool aHeld) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void Apply() noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Show(BodyPlayback& aPlayback, const BodyPlayback::TickOf& acTickOf, uint64_t aNow) noexcept;
    void PlaceRests(uint64_t aNow) noexcept;
    // A reference's playback time at aNow (the frame clock).
    [[nodiscard]] uint64_t PlaybackTick(uint64_t aNow) const noexcept;
    // A copy's ragdoll's time: its copy's (InterpolationSystem::PlaybackTick), 0 without one.
    [[nodiscard]] uint64_t CopyTickOf(uint32_t aSessionId) noexcept;
    [[nodiscard]] BodyPlayback& PlaybackOf(const BodyKey& acKey) noexcept { return acKey.Actor ? m_ragdolls : m_playback; }
    // This game's entity for a copy, entt::null without one.
    [[nodiscard]] entt::entity CopyEntity(uint32_t aSessionId) const noexcept;
    // This game's copy of that character (its form id), 0 for none.
    [[nodiscard]] uint32_t CopyOf(uint32_t aSessionId) const noexcept;
    // This game's own actor of that session id (its corpse another game's player moves), or none.
    [[nodiscard]] uint32_t OwnActorOf(uint32_t aSessionId) const noexcept;
    // The actor a ragdoll's frames play here: its copy, or this game's own when another game's player moves it.
    [[nodiscard]] uint32_t ActorOf(uint32_t aSessionId) const noexcept;

    World& m_world;
    IBodyBridge& m_bridge;
    Keeper* m_pKeeper{};
    BodyPlayback m_playback; // references
    BodyPlayback m_ragdolls; // copies' ragdolls
    std::map<BodyKey, Resting> m_rests;                               // references at another game's rest
    std::set<BodyKey> m_placed; // their newest rest is where the host's game put it (BodyState::Placed)
    std::map<BodyKey, std::pair<uint64_t, BodyPose>> m_ragdollRests; // a corpse another game's player moved, at its rest
    std::set<uint32_t> m_declaredRests; // this game's actors whose ragdoll rest is declared (cleared as one wakes)
    // Apply's copies by session id: a lookup per body and bone each frame, not a scan of the copies.
    std::unordered_map<uint32_t, entt::entity> m_copyIndex;
    bool m_copiesIndexed{};
    std::set<BodyKey> m_following; // played from another game's states (logged once)
    std::map<BodyKey, uint32_t> m_held; // held in another game: its form id here
    std::set<BodyKey> m_missing;   // not in this game (logged once)
    std::set<uint32_t> m_heldElsewhere;
    std::vector<entt::scoped_connection> m_connections;
    bool m_updateQueued{};
    MainLoopTasks::Scope m_mainTasks;
};
