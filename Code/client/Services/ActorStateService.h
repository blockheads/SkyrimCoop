#pragma once

#include <Events/Listen.h>
#include <Structs/ActorStatePart.h>
#include <Structs/RecordPart.h>

#include <NoCopyMove.h>

#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

class IActorBridge;
struct World;
struct TransportService;
struct CopyRecords;
struct UpdateEvent;
struct DisconnectedEvent;
struct FactionsChangeEvent;

/**
 * @brief Actors' state beside their graph (sneak, block, sprint, life, kill move, drawn hands) as a record part
 * (docs/design/one-simulator.md, piece 2).
 *
 * The owner reads it after each frame and declares it when it changed; a copy takes it once its stream's playback
 * reaches the frame it was read at (ActorStateRules). Its factions and its display name are parts of their own:
 * factions declared on the engine's faction writes (FactionEvents), the name read after each frame as the state is
 * (a script's SetDisplayName or SetName raises nothing).
 */
struct ActorStateService
{
    ActorStateService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge, CopyRecords& aRecords) noexcept;
    ~ActorStateService() noexcept;

    NO_COPY_MOVE(ActorStateService);

    // One of this game's actors' state now; aLast: its last part, whose hands stay while they move.
    [[nodiscard]] ActorStatePart ReadPart(uint32_t aFormId, const ActorStatePart* apLast = nullptr) const noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnFactionsChange(const FactionsChangeEvent& acEvent) noexcept;
    // The factions part of aId on its copy.
    void ApplyFactions(uint32_t aId, const std::string& acBytes) noexcept;
    void ApplyName(uint32_t aId, const std::string& acName) noexcept;
    // One of this game's actors: its part of aKind declared when it differs from the one the record holds (true).
    bool DeclareIfChanged(uint32_t aId, RecordKind aKind, std::string aBytes) noexcept;
    void OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    struct Applied
    {
        bool Done;                     // false: looked at again next frame (its stream behind it, hands mid-animation,
                                       // a kill move its owner left while its pair still plays)
        std::vector<uint32_t> Missing; // the session ids it names that are not here
    };
    // The record's part of aId on its copy.
    [[nodiscard]] Applied TryApply(uint32_t aId) noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    CopyRecords& m_records;

    // Per owned actor (session id): the part last declared.
    std::unordered_map<uint32_t, ActorStatePart> m_sent;
    // Copies whose newest part waits for their stream.
    std::set<uint32_t> m_due;

    std::vector<entt::scoped_connection> m_connections;
};
