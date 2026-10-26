#pragma once

#include <Events/Listen.h>
#include <Events/PlayerDeathEvent.h>
#include <Messages/DeathMessages.h>

#include <NoCopyMove.h>

#include <optional>
#include <string>

struct World;
struct TransportService;
class IPlayerBridge;
class IActorBridge;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ActorDeathChangeEvent;
struct NotifySettingsChange;
struct PlayerArrivedEvent;
struct NotifyPartyTravel;
struct NotifyCharacterSpawn;
struct ReferenceLoadedEvent;
struct ActorRemovedEvent;

/**
 * @brief This game's player going down and coming back (docs/design/death.md), on every game:
 *
 *   down       this game's player's real death (TESDeathEvent; the reload held while in a session): its controls
 *              held, the host told (one generation per fall), then it spectates the first standing player (left/right
 *              cycle) once it is dead
 *   party      the host's NotifyPartyDeath: who is down, the party's combat, the recovery countdown, a wipe
 *   respawn    the host's NotifyPlayerRespawn for this fall: it follows to beside a standing player, then is revived
 *              there, its controls and camera its own again
 *
 * The other players' copies die by their owners' death actions, and are made again alive on a revive (a resurrected
 * copy's graph stays in its death state: it refuses every action). The host's rules are HostDeath's.
 */
struct DeathService
{
    DeathService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IPlayerBridge& aPlayerBridge,
                 IActorBridge& aActorBridge) noexcept;
    ~DeathService() noexcept;

    NO_COPY_MOVE(DeathService);

    [[nodiscard]] bool IsDown() const noexcept { return m_down; }
    [[nodiscard]] uint32_t DownGeneration() const noexcept { return m_generation; }
    // The session id of the character watched, 0 for none.
    [[nodiscard]] uint32_t Spectating() const noexcept { return m_spectating; }
    // What the death screen shows: whose character is watched, and the party's state.
    [[nodiscard]] std::string SpectatingName() const noexcept;
    [[nodiscard]] std::string Status() const noexcept;

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnSettings(const NotifySettingsChange& acMessage) noexcept;

    // This game's player.
    void OnPlayerDeath(const ActorDeathChangeEvent& acEvent) noexcept;
    // aDying: its death still playing out (dying, not yet dead).
    void GoDown(bool aDying) noexcept;
    void OnRespawn(const NotifyPlayerRespawn& acMessage) noexcept;
    void OnPartyTravel(const NotifyPartyTravel& acMessage) noexcept;
    void OnRespawnArrival(const PlayerArrivedEvent& acEvent) noexcept;
    // Not down any more (revived, disconnected, gone): the camera and controls its own again.
    void StandUp() noexcept;
    void SendDeathState() const noexcept;

    // The party, and who this game's player watches.
    void OnPartyDeath(const NotifyPartyDeath& acMessage) noexcept;
    void OnCycle(const SpectateCycleEvent& acEvent) noexcept;
    // The next standing player with a loaded copy (aDirection: 0 the same or the first, +1 next, -1 previous).
    void ChooseSpectator(int aDirection = 0) noexcept;
    // A copy loaded, left or came back, or a script took or gave back the camera: chosen again, next frame.
    void QueueRefresh() noexcept;
    void OnReferenceLoaded(const ReferenceLoadedEvent&) noexcept { QueueRefresh(); }
    void OnCopySpawn(const NotifyCharacterSpawn&) noexcept { QueueRefresh(); }
    void OnRemoved(const ActorRemovedEvent&) noexcept { QueueRefresh(); }
    void OnScreenChanged(const SpectatorRefreshEvent&) noexcept { QueueRefresh(); }

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IPlayerBridge& m_playerBridge;
    IActorBridge& m_actorBridge;

    uint32_t m_characterId{}; // this game's player's character's session id
    bool m_announced{};       // its death state sent once it has its session id
    bool m_down{};
    uint32_t m_generation{};  // its falls so far: a host message for an older one is stale
    bool m_dying{};           // dying, not yet dead: no spectating yet
    std::optional<NotifyPlayerRespawn> m_respawn; // the host's respawn, travelling to it (the party may move on)

    NotifyPartyDeath m_party;                       // the host's last word
    double m_remaining{};                           // its recovery countdown, counted down here for the screen
    uint32_t m_spectating{};
    uint32_t m_shown{};       // the copy the camera shows (0 while a script holds the camera: kept, not shown)
    bool m_refreshQueued{};

    std::vector<entt::scoped_connection> m_connections;
};
