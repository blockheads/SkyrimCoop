#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>

#include <Services/QuestService.h>
#include <Services/ImguiService.h>

#include <World.h>

#include <Bridges/IFormBridge.h>
#include <Bridges/IEventBridge.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>

namespace
{
// Quest sync waits for its host-authority design (docs/design/host-authority-migration.md): the
// relay here would carry every player's progress both ways.
constexpr bool cSyncQuests = false;
} // namespace

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_formBridge(aWorld.ctx().at<IFormBridge&>())
    , m_eventBridge(aWorld.ctx().at<IEventBridge&>())
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);

    // A note about the Gameevents:
    // TESQuestStageItemDoneEvent gets fired to late, we instead use TESQuestStageEvent, because it responds immediately.
    // TESQuestInitEvent can be instead managed by start stop quest management.
    // Register game event listeners via the bridge instead of direct EventDispatcherManager calls.
    m_eventBridge.RegisterQuestStartStopSink(
        [this](uint32_t formId, bool started) { OnQuestStartStop(formId, started); });
    m_eventBridge.RegisterQuestStageSink(
        [this](uint32_t formId, uint32_t stageId) { OnQuestStage(formId, stageId); });
}

void QuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // TODO: this should be followed with whatever the quest leader selected
    /*
    // deselect any active quests
    for (auto questId : m_playerBridge.GetPlayerActiveQuestFormIds())
        m_formBridge.SetQuestActive(questId, false);
    */
}

void QuestService::OnQuestStartStop(uint32_t aFormId, bool aStarted) noexcept
{
    if (m_formBridge.IsQuestOverrideActive() || !cSyncQuests)
        return;

    spdlog::info("Quest start/stop event: {:X}", aFormId);

    if (!m_formBridge.QuestExists(aFormId))
        return;

    if (IsNonSyncableQuest(aFormId, m_formBridge))
        return;

    const uint8_t questType = m_formBridge.GetQuestType(aFormId);
    if (questType == 0 /* TESQuest::Type::None */ || questType == 1 /* TESQuest::Type::Miscellaneous */)
    {
        GameId Id;
        auto& modSys = m_world.GetModSystem();
        if (modSys.GetServerModId(aFormId, Id))
        {
            spdlog::info("{}: : queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}", __FUNCTION__,
                         Id.LogFormat(), m_formBridge.GetQuestStage(aFormId),
                         m_formBridge.IsQuestStopped(aFormId) ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started,
                         questType,
                         aFormId, m_formBridge.GetQuestName(aFormId));
        }
    }

    const bool stopped = m_formBridge.IsQuestStopped(aFormId);
    const uint32_t stage = m_formBridge.GetQuestStage(aFormId);

    m_world.GetRunner().Queue(
        [this, formId = aFormId, stageId = stage, stopped, questType]()
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(formId, Id))
            {
                RequestQuestUpdate update;
                update.Id = Id;
                update.Stage = stageId;
                update.Status = stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started;
                update.ClientQuestType = questType;

                m_world.GetTransport().Send(update);
            }
        });
}

void QuestService::OnQuestStage(uint32_t aFormId, uint32_t aStageId) noexcept
{
    if (m_formBridge.IsQuestOverrideActive() || !cSyncQuests)
        return;

    spdlog::info("Quest stage event: {:X}, stage: {}", aFormId, aStageId);

    if (!m_formBridge.QuestExists(aFormId))
        return;

    if (IsNonSyncableQuest(aFormId, m_formBridge))
        return;

    const uint8_t questType = m_formBridge.GetQuestType(aFormId);
    if (questType == 0 /* TESQuest::Type::None */ || questType == 1 /* TESQuest::Type::Miscellaneous */)
    {
        GameId Id;
        auto& modSys = m_world.GetModSystem();
        if (modSys.GetServerModId(aFormId, Id))
        {
            spdlog::info("{}: : queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}", __FUNCTION__,
                         Id.LogFormat(), aStageId,
                         RequestQuestUpdate::StageUpdate,
                         questType,
                         aFormId, m_formBridge.GetQuestName(aFormId));
        }
    }

    m_world.GetRunner().Queue(
        [this, formId = aFormId, stageId = aStageId, questType]()
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(formId, Id))
            {
                RequestQuestUpdate update;
                update.Id = Id;
                update.Stage = stageId;
                update.Status = RequestQuestUpdate::StageUpdate;
                update.ClientQuestType = questType;

                m_world.GetTransport().Send(update);
            }
        });
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();
    uint32_t formId = modSystem.GetGameId(aUpdate.Id);

    if (!m_formBridge.QuestExists(formId))
    {
        spdlog::error("Failed to find quest, base id: {:X}, mod id: {:X}", aUpdate.Id.BaseId, aUpdate.Id.ModId);
        return;
    }

    const uint8_t questType = m_formBridge.GetQuestType(formId);
    if (questType == 0 || questType == 1)
    {
        spdlog::info("{}: : receiving type none/misc quest update gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}", __FUNCTION__,
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status,
                     aUpdate.ClientQuestType, formId, m_formBridge.GetQuestName(formId));
    }

    bool bResult = false;
    switch (aUpdate.Status)
    {
    case NotifyQuestUpdate::Started:
    {
        m_formBridge.ScriptSetQuestStage(formId, aUpdate.Stage);
        m_formBridge.SetQuestActive(formId, true);
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    }
    case NotifyQuestUpdate::StageUpdate:
        m_formBridge.ScriptSetQuestStage(formId, aUpdate.Stage);
        bResult = true;
        spdlog::info("Remote quest updated: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    case NotifyQuestUpdate::Stopped:
        bResult = StopQuest(formId, m_formBridge);
        spdlog::info("Remote quest stopped: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    default: break;
    }

    if (!bResult)
        spdlog::error("Failed to update the client quest state, quest: {:X}, stage: {}, status: {}", formId, aUpdate.Stage, aUpdate.Status);
}

bool QuestService::StopQuest(uint32_t aFormId, IFormBridge& aFormBridge)
{
    if (aFormBridge.QuestExists(aFormId))
    {
        aFormBridge.SetQuestActive(aFormId, false);
        aFormBridge.SetQuestStopped(aFormId);
        return true;
    }

    return false;
}

static constexpr std::array kNonSyncableQuestIds = std::to_array<uint32_t>({
    0x2BA16,   // Werewolf transformation quest
    0x20071D0, // Vampire transformation quest
    0x3AC44,   // MS13BleakFallsBarrowLeverScene
    // 0xFE014801,  // Unknown dynamic ID, kept as note, maybe lookup correct ID this game?
    0xF2593 // Skill experience quest
});

bool QuestService::IsNonSyncableQuest(uint32_t aFormId, const IFormBridge& aFormBridge)
{
    // Quests with no quest stages are never synced. Most TESQues::Type:: quests should
    // be synced, including Type::None and Type::Miscellaneous, but there are a few
    // known exceptions that should be excluded that are in the table.
    return    aFormBridge.IsQuestStagesEmpty(aFormId)
           || std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), aFormId) != kNonSyncableQuestIds.end();
}

void QuestService::DebugDumpQuests()
{
    // TODO(02.5-02): implement via IFormBridge when GetAllQuestFormIds() is available
    spdlog::info("QuestService::DebugDumpQuests: not yet implemented via bridge");
}
