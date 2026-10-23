#pragma once

#include <NoCopyMove.h>
#include <Structs/GameId.h>

class IActorBridge;

struct World;
struct TransportService;
struct EffectVisualsChangeEvent;
struct ReferenceLoadedEvent;
struct NotifyActiveEffects;
struct NotifyEffectFlash;
struct NotifyCharacterSpawn;
struct NotifyRemoveCharacter;
struct DisconnectedEvent;

/**
 * @brief What magic effects show on actors (burning, frost, a healing glow), as shared state.
 *
 * An actor's owner applies its effects; copies never do (docs/design/copy-contract.md). The owner
 * shares which effects with lasting visuals (FX Persist) are active on its actors when the engine starts or ends
 * one (RequestActiveEffects); a spawn carries them for a copy made later. Each copy plays only their hit
 * visuals, from when an effect appears in its owner's list until it leaves it, like Unreal's gameplay
 * cues, and again when its 3D loads (the engine drops them with it). Visuals that play once go out when they
 * start (EffectFlashRequest), and each copy plays them once.
 */
struct EffectVisualsService
{
    EffectVisualsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;
    ~EffectVisualsService() noexcept = default;

    NO_COPY_MOVE(EffectVisualsService);

    // The effects with visuals active on an actor of this game: what its copies show.
    [[nodiscard]] std::vector<GameId> ReadEffects(uint32_t aFormId) const noexcept;

protected:
    void OnEffectVisualsChange(const EffectVisualsChangeEvent& acEvent) noexcept;
    void OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept;
    void OnNotifyActiveEffects(const NotifyActiveEffects& acMessage) noexcept;
    void OnNotifyEffectFlash(const NotifyEffectFlash& acMessage) noexcept;
    void OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

private:
    // The effect form ids with visuals active on an actor of this game, sorted.
    [[nodiscard]] std::vector<uint32_t> ReadEffectForms(uint32_t aFormId) const noexcept;
    void SendOwned(uint32_t aFormId, uint32_t aSessionId) noexcept;
    // One of this game's actors started an effect's visuals that play once: its copies play them.
    void SendFlash(uint32_t aSessionId, uint32_t aEffectFormId) noexcept;
    // A copy's owner lists these: the visuals of those that left it stop, the missing play.
    void Show(uint32_t aSessionId, std::vector<uint32_t> aEffectIds) noexcept;
    void PlayMissing(uint32_t aSessionId) noexcept;
    void StopShown(uint32_t aSessionId) noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;

    // Per owned actor (session id): the effect form ids last sent.
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_sent;
    // Per copy (session id): the effects its owner lists, and the reference they were last played on.
    struct Shown
    {
        std::vector<uint32_t> EffectIds;
        uint32_t FormId{};
    };
    std::unordered_map<uint32_t, Shown> m_shown;

    entt::scoped_connection m_changeConnection;
    entt::scoped_connection m_loadedConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_flashConnection;
    entt::scoped_connection m_spawnConnection;
    entt::scoped_connection m_removeConnection;
    entt::scoped_connection m_disconnectConnection;
};
