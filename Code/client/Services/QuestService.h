#pragma once

struct World;
struct NotifyQuestUpdate;
class IFormBridge;
class IEventBridge;

/**
 * @brief Handles quest sync
 *
 * This service is currently not in use.
 */
class QuestService final
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    TP_NOCOPYMOVE(QuestService);

    static bool IsNonSyncableQuest(uint32_t aFormId, const IFormBridge& aFormBridge);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aFormId, IFormBridge& aFormBridge);

private:
    void OnConnected(const ConnectedEvent&) noexcept;

    // Called by IEventBridge when a quest start/stop event fires.
    void OnQuestStartStop(uint32_t aFormId, bool aStarted) noexcept;
    // Called by IEventBridge when a quest stage event fires.
    void OnQuestStage(uint32_t aFormId, uint32_t aStageId) noexcept;

    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;

    World& m_world;
    IFormBridge& m_formBridge;
    IEventBridge& m_eventBridge;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
};
