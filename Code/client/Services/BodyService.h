#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <Physics/BodyPlayback.h>

#include <NoCopyMove.h>

#include <set>
#include <vector>

struct World;
struct UpdateEvent;
struct DisconnectedEvent;
class IBodyBridge;

/**
 * @brief Every game plays the bodies other games own (docs/design/physics-sync.md, "Owners"):
 *
 *   frames   played kDelayMs behind the session's time (BodyPlayback): each body put at the pose each update, held
 *            keyframed, its node with it; a body this game's own physics moved first is blended from there. A copy's
 *            ragdoll plays on the copies' clock (kCopyDelayMs, with its movement), steered
 *   rest     placed exactly; a reference's simulated again (asleep) so this game's player can push and grab it
 *
 * A guest gets the host's bodies and the others' held ones; the host a guest's held ones. The bodies this game owns
 * for now (a guest's player pushed or holds them) are its Keeper's: their frames wait.
 */
struct BodyService
{
    // A reference's frames, behind the owner's time: a frame due now has arrived (half a round trip and a step's
    // jitter). A copy's ragdoll's: kCopyDelayMs (CopyClock.h).
    static constexpr uint64_t kDelayMs = 100;

    // This game's own bodies among the session's (a guest's hands, GuestHands): none of them is played here.
    struct Keeper
    {
        virtual ~Keeper() = default;
        // Each update, before the played bodies move: the bodies that became this game's own (their playback ends);
        // acPlayed: the bodies the owners' frames move here now.
        [[nodiscard]] virtual std::vector<BodyKey> Update(uint64_t aTick, const std::set<BodyKey>& acPlayed) noexcept = 0;
        // An owner's frame of that body: false while it is this game's own (the frame waits).
        [[nodiscard]] virtual bool TakesFrame(const BodyKey& acKey) noexcept = 0;
        // Its rest is not placed while this game's player holds it.
        [[nodiscard]] virtual bool KeepsRest(const BodyKey& acKey) const noexcept = 0;
        // Its owner's rest placed here: the owner's again.
        virtual void OnRest(const BodyPose& acRest) noexcept = 0;
    };

    BodyService(World& aWorld, entt::dispatcher& aDispatcher, IBodyBridge& aBridge) noexcept;

    NO_COPY_MOVE(BodyService);

    void SetKeeper(Keeper* apKeeper) noexcept { m_pKeeper = apKeeper; }
    // This game's own physics has the body again (a guest let go of the host's): its playback ends.
    void Forget(const BodyKey& acKey) noexcept;

private:
    void OnFrames(const NotifyBodyFrames& acMessage) noexcept;
    void OnRests(const NotifyBodyRests& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Show(BodyPlayback& aPlayback, uint64_t aTick) noexcept;
    [[nodiscard]] BodyPlayback& PlaybackOf(const BodyKey& acKey) noexcept { return acKey.Actor ? m_ragdolls : m_playback; }
    // This game's copy of that character (its form id), 0 for none.
    [[nodiscard]] uint32_t CopyOf(uint32_t aSessionId) const noexcept;

    World& m_world;
    IBodyBridge& m_bridge;
    Keeper* m_pKeeper{};
    BodyPlayback m_playback; // references
    BodyPlayback m_ragdolls; // copies' ragdolls
    std::set<BodyKey> m_following; // played from an owner's frames (logged once)
    std::set<BodyKey> m_missing;   // not in this game (logged once)
    std::vector<entt::scoped_connection> m_connections;
};
