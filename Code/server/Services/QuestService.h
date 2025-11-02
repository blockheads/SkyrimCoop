#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>

struct RequestQuestUpdate;

namespace Server
{

struct World;
struct UpdateEvent;

/**
 * @brief Dispatch quest sync messages.
 *
 * This service is currently not in use.
 */
class QuestService
{
public:
    QuestService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnQuestChanges(const PacketEvent<RequestQuestUpdate>& aChanges) noexcept;

    World& m_world;

    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
};

} // namespace Server
