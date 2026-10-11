#pragma once

#include <Events/PacketEvent.h>
#include <Structs/ActorData.h>

struct AssignCharacterRequest;
struct CharacterSpawnRequest;
struct ClientReferencesMoveRequest;
struct GridCellCoords;
struct RequestRespawn;
struct RequestRemoveCharacter;

namespace Server
{

struct CharacterSpawnedEvent;
struct PlayerEnterWorldEvent;
struct World;
struct CharacterRemoveEvent;

/**
 * @brief Manages player and actor state.
 */
struct CharacterService
{
    CharacterService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~CharacterService() noexcept = default;

    TP_NOCOPYMOVE(CharacterService);

    static void Serialize(World& aRegistry, entt::entity aEntity, CharacterSpawnRequest* apSpawnRequest) noexcept;

protected:
    void OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept;
    void OnCharacterRemoveEvent(const CharacterRemoveEvent& acEvent) const noexcept;
    void OnRequestRemoveCharacter(const PacketEvent<RequestRemoveCharacter>& acMessage) const noexcept;
    void OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) const noexcept;
    void OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) const noexcept;
    void OnRequestRespawn(const PacketEvent<RequestRespawn>& acMessage) const noexcept;

    void CreateCharacter(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept;
    ActorData BuildActorData(const entt::entity acEntity) const noexcept;
    void ApplyActorData(const entt::entity acEntity, const ActorData& acActorData) const noexcept;
    void BroadcastActorData(Player* apPlayer, const entt::entity acEntity, const ActorData& acActorData) const noexcept;

private:
    World& m_world;

    entt::scoped_connection m_characterAssignRequestConnection;
    entt::scoped_connection m_removeCharacterConnection;
    entt::scoped_connection m_requestRemoveCharacterConnection;
    entt::scoped_connection m_characterSpawnedConnection;
    entt::scoped_connection m_playerEnterWorldConnection;
    entt::scoped_connection m_requestRespawnConnection;
};

} // namespace Server
