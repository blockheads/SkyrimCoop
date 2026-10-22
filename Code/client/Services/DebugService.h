#pragma once

#include <Actor.h>

struct World;
struct ImguiService;

struct UpdateEvent;
struct DialogueEvent;
struct SubtitleEvent;
struct MoveActorEvent;

struct TransportService;
struct BSAnimationGraphManager;

/**
 * @brief Manages the debuggers.
 */
struct DebugService
{
    DebugService(entt::dispatcher& aDispatcher, World& aWorld, TransportService& aTransport, ImguiService& aImguiService);
    ~DebugService() noexcept = default;

    NO_COPY_MOVE(DebugService);

    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDialogue(const DialogueEvent&) noexcept;
    void OnSubtitle(const SubtitleEvent&) noexcept;
    void OnMoveActor(const MoveActorEvent&) noexcept;

    void SetDebugId(const uint32_t aFormId) noexcept;

    static void ArrangeGameWindows(HWND aThisWindow) noexcept;

protected:
    void OnDraw() noexcept;

private:
    void PlaceActorInWorld() noexcept;
    void DisplayEntities() noexcept;
    void DisplayEntityPanel(entt::entity aEntity) noexcept;
    void DisplayFormComponent(FormIdComponent& aFormComponent) const noexcept;
    void DisplayLocalComponent(LocalComponent& aLocalComponent, const uint32_t acFormId) const noexcept;
    void DisplayRemoteComponent(RemoteComponent& aLocalComponent, const entt::entity acEntity, const uint32_t acFormId) const noexcept;

    void DrawServerView() noexcept;
    void DrawEntitiesView();
    void DrawComponentDebugView();
    void DrawPlayerDebugView();
    void DrawAnimDebugView();
    void DrawContainerDebugView();
    void DrawFormDebugView();
    void DrawSkillView();
    void DrawNetworkView();
    void DrawSessionView();
    void DrawActorValuesView();
    void DrawQuestDebugView();
    void DrawCellView();
    void DrawProcessView();
    void DrawWeatherView();
    void DrawCombatView();
    void DrawCalendarView();
    void DrawDragonSpawnerView();

public:
    bool m_showDebugStuff = false;

private:
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    World& m_world;

    std::vector<GamePtr<Actor>> m_actors;

    uint32_t m_formId = 0;

    uint32_t ActorID = 0;
    std::string VoiceFile = "";

    uint32_t SubActorID = 0;
    std::string SubtitleText = "";
    uint32_t TopicID = 0;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_drawImGuiConnection;
    entt::scoped_connection m_dialogueConnection;
    bool m_showBuildTag = true;
    bool m_drawComponentsInWorldSpace = false;
};
