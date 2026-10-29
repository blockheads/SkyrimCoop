
#include <Havok/hkbStateMachine.h>
#include <Structs/AnimationGraphDescriptorManager.h>

#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/hkbBehaviorGraph.h>

#include <Services/ImguiService.h>
#include <Services/DebugService.h>
#include <Services/TransportService.h>
#include <Services/PapyrusService.h>

#include <Events/UpdateEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/SubtitleEvent.h>
#include <Events/MoveActorEvent.h>

#include <Games/References.h>

#include <BSAnimationGraphManager.h>
#include <Forms/TESFaction.h>
#include <Forms/TESQuest.h>

#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>
#include <Forms/TESNPC.h>
#include <Games/Animation/ActorMediator.h>
#include <Games/Animation/TESActionData.h>
#include <Magic/ActorMagicCaster.h>
#include <Misc/BSFixedString.h>
#include <Structs/ActionEvent.h>

#include <Components.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>

#include <AI/AIProcess.h>

#include <Messages/RequestRespawn.h>

#include <Games/Misc/SubtitleManager.h>
#include <Games/Overrides.h>

#include <EquipManager.h>
#include <Forms/TESAmmo.h>
#include <BSGraphics/BSGraphicsRenderer.h>
#include <Interface/UI.h>

#include <Combat/CombatController.h>
#include <Camera/PlayerCamera.h>
#include <AI/Movement/PlayerControls.h>
#include <Interface/IMenu.h>
#include <Camera/PlayerCamera.h>
#include <DefaultObjectManager.h>
#include <Misc/InventoryEntry.h>
#include <Misc/MiddleProcess.h>

#include <imgui.h>
#include <inttypes.h>
#include <BuildVersion.h>
#include <RTTI.h>
#include <Services/HostService.h>
extern thread_local bool g_overrideFormId;

static void DrawBuildTag()
{
    auto* pWindow = BSGraphics::GetMainWindow();
    const ImVec2 coord{50.f, static_cast<float>((pWindow->uiWindowHeight + 25) - 100)};
    static const std::string cBuildTag = std::format("Build: {} {} EVO\nBuilt: " __TIMESTAMP__, BuildCommit(), BuildBranch());
    ImGui::GetBackgroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), coord, ImColor(255.f, 0.f, 0.f), cBuildTag.c_str());
}

DebugService::DebugService(entt::dispatcher& aDispatcher, World& aWorld, TransportService& aTransport, ImguiService& aImguiService)
    : m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_world(aWorld)
{
    Listen<UpdateEvent, &DebugService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<&DebugService::OnDraw>(m_connections, aImguiService.OnDraw, this);
    Listen<DialogueEvent, &DebugService::OnDialogue>(m_connections, m_dispatcher, this);
    m_dispatcher.sink<SubtitleEvent>().connect<&DebugService::OnSubtitle>(this);
    m_dispatcher.sink<MoveActorEvent>().connect<&DebugService::OnMoveActor>(this);
}

void DebugService::OnDialogue(const DialogueEvent& acEvent) noexcept
{
    if (ActorID)
        return;
    ActorID = acEvent.ActorID;
    VoiceFile = acEvent.VoiceFile;
}

void DebugService::OnSubtitle(const SubtitleEvent& acEvent) noexcept
{
    if (SubActorID)
        return;
    SubActorID = acEvent.SpeakerID;
    SubtitleText = acEvent.Text;
    TopicID = acEvent.TopicFormID;
}

// TODO: yeah, i'm aware of how dumb this looks, but things crash if
// you do it directly by adding an event to the queue, no symbols for tiltedcore when debugging,
// so this'll do for now
struct MoveData
{
    Actor* pActor = nullptr;
    TESObjectCELL* pCell = nullptr;
    NiPoint3 position;
} moveData;

void DebugService::OnMoveActor(const MoveActorEvent& acEvent) noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(acEvent.CellId));

    if (!pActor || !pCell)
        return;

    moveData.pActor = pActor;
    moveData.pCell = pCell;
    moveData.position = acEvent.Position;
}

void DebugService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    if (!BSGraphics::GetMainWindow()->IsForeground())
        return;

    if (moveData.pActor)
    {
        moveData.pActor->MoveTo(moveData.pCell, moveData.position);
        moveData.pActor = nullptr;
    }

    if (GetAsyncKeyState(VK_F3) & 0x01)
    {
        m_showDebugStuff = !m_showDebugStuff;
    }

}

static bool g_enableConnectionWindow{false};
static bool g_enableAnimWindow{false};
static bool g_enableEntitiesWindow{false};
static bool g_enableInventoryWindow{false};
static bool g_enableNetworkWindow{false};
static bool g_enableFormsWindow{false};
static bool g_enablePlayerWindow{false};
static bool g_enableSkillsWindow{false};
static bool g_enableSessionWindow{false};
static bool g_enableActorValuesWindow{false};
static bool g_enableQuestWindow{false};
static bool g_enableCellWindow{false};
static bool g_enableProcessesWindow{false};
static bool g_enableWeatherWindow{false};
static bool g_enableCombatWindow{false};
static bool g_enableCalendarWindow{false};
static bool g_enableDragonSpawnerWindow{false};

void DebugService::DrawConnectionView() noexcept
{
    ImGui::SetNextWindowSize(ImVec2(250, 440), ImGuiCond_FirstUseEver);
    ImGui::Begin("Connection");

    static char s_address[1024] = "127.0.0.1:10578";
    ImGui::InputText("Address", s_address, std::size(s_address));

    if (m_transport.IsOnline())
    {
        if (ImGui::Button("Disconnect"))
            m_transport.Close();
    }
    else
    {
        if (ImGui::Button("Connect"))
        {
            m_transport.Connect(s_address);
        }
    }

    ImGui::End();
}

void DebugService::OnDraw() noexcept
{
    const auto view = m_world.view<FormIdComponent>();
    if (view.empty() || !m_showDebugStuff)
        return;

    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("Helpers"))
    {
        if (ImGui::Button("Unstuck player"))
        {
            auto* pPlayer = PlayerCharacter::Get();
            pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);
        }

        if (ImGui::Button("Stop all combat"))
        {
            auto* pPlayer = PlayerCharacter::Get();
            pPlayer->PayCrimeGoldToAllFactions();

            ProcessLists* const pProcessLists = ProcessLists::Get();
            if (pProcessLists)
            {
                for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
                {
                    Actor* const pRefr = Cast<Actor>(TESObjectREFR::GetByHandle(pProcessLists->highActorHandleArray[i]));
                    if (pRefr && pRefr->GetNiNode())
                        pRefr->StopCombat();
                }
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Components"))
    {
        ImGui::MenuItem("Show selected entity in world", nullptr, &m_drawComponentsInWorldSpace);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI"))
    {
        ImGui::MenuItem("Show build tag", nullptr, &m_showBuildTag);
        if (ImGui::Button("Log all open windows"))
        {
            UI* pUI = UI::Get();
            for (const auto& it : pUI->menuMap)
            {
                if (pUI->GetMenuOpen(it.key))
                    spdlog::info("{}", it.key.AsAscii());
            }
        }

        if (ImGui::Button("Close all menus"))
        {
            UI::Get()->CloseAllMenus();
        }

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Debuggers"))
    {
        ImGui::MenuItem("Quests", nullptr, &g_enableQuestWindow);
        ImGui::MenuItem("Entities", nullptr, &g_enableEntitiesWindow);
        ImGui::MenuItem("Connection", nullptr, &g_enableConnectionWindow);
        ImGui::MenuItem("Session", nullptr, &g_enableSessionWindow);
        ImGui::MenuItem("Dragon spawner", nullptr, &g_enableDragonSpawnerWindow);

        ImGui::MenuItem("Network", nullptr, &g_enableNetworkWindow);
        ImGui::MenuItem("Forms", nullptr, &g_enableFormsWindow);
        ImGui::MenuItem("Inventory", nullptr, &g_enableInventoryWindow);
        ImGui::MenuItem("Animations", nullptr, &g_enableAnimWindow);
        ImGui::MenuItem("Player", nullptr, &g_enablePlayerWindow);
        ImGui::MenuItem("Skills", nullptr, &g_enableSkillsWindow);
        ImGui::MenuItem("Cell", nullptr, &g_enableCellWindow);
        ImGui::MenuItem("Processes", nullptr, &g_enableProcessesWindow);
        ImGui::MenuItem("Weather", nullptr, &g_enableWeatherWindow);
        ImGui::MenuItem("Combat", nullptr, &g_enableCombatWindow);
        ImGui::MenuItem("Calendar", nullptr, &g_enableCalendarWindow);

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Misc"))
    {
        if (ImGui::Button("Crash Client"))
        {
            spdlog::error("Crash client");
            int* m = 0;
            *m = 1338;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();

    if (g_enableQuestWindow)
        DrawQuestDebugView();
    if (g_enableEntitiesWindow)
        DrawEntitiesView();
    if (g_enableConnectionWindow)
        DrawConnectionView();
    if (g_enableSessionWindow)
        DrawSessionView();
    if (g_enableDragonSpawnerWindow)
        DrawDragonSpawnerView();

    if (g_enableNetworkWindow)
        DrawNetworkView();
    if (g_enableFormsWindow)
        DrawFormDebugView();
    if (g_enableInventoryWindow)
        DrawContainerDebugView();
    if (g_enableAnimWindow)
        DrawAnimDebugView();
    if (g_enablePlayerWindow)
        DrawPlayerDebugView();
    if (g_enableSkillsWindow)
        DrawSkillView();
    if (g_enableActorValuesWindow)
        DrawActorValuesView();
    if (g_enableCellWindow)
        DrawCellView();
    if (g_enableProcessesWindow)
        DrawProcessView();
    if (g_enableWeatherWindow)
        DrawWeatherView();
    if (g_enableCombatWindow)
        DrawCombatView();
    if (g_enableCalendarWindow)
        DrawCalendarView();

    if (m_drawComponentsInWorldSpace)
        DrawComponentDebugView();

    if (m_showBuildTag)
        DrawBuildTag();
}

void DebugService::ArrangeGameWindows(HWND aThisWindow) noexcept
{
    // This function conveniently arranges multiple game windows (multi-monitor window rearrangement is
    // not implemented)
    const uint32_t screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const uint32_t screenHeight = GetSystemMetrics(SM_CYSCREEN);
    RECT rect{};
    GetWindowRect(aThisWindow, &rect);
    const uint32_t gameWindowWidth = rect.right - rect.left;
    const uint32_t gameWindowHeight = rect.bottom - rect.top;

    const bool shouldArrangeWindows = (static_cast<float>(gameWindowWidth) / screenWidth) < 0.76f;
    if (!shouldArrangeWindows)
        return; // Too little room for that

    SetWindowPos(GetConsoleWindow(), nullptr, 0, gameWindowHeight, 0, 0, SWP_NOSIZE);

    CreateMutexA(0, FALSE, "SkyrimTogetherWindowMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // One game instance is already open, arrange secondary game window...
        const bool arrangeSideBySide = (static_cast<float>(gameWindowWidth) / screenWidth) <= 0.51f;

        RECT conRect{};
        GetWindowRect(GetConsoleWindow(), &conRect);
        const uint32_t conY = arrangeSideBySide ? gameWindowHeight : 0;
        const uint32_t conX = arrangeSideBySide ? (gameWindowWidth - 16) : screenWidth - (conRect.right - conRect.left);
        SetWindowPos(GetConsoleWindow(), nullptr, conX, conY, 0, 0, SWP_NOSIZE);

        const uint32_t x = arrangeSideBySide ? (gameWindowWidth - 16) : (screenWidth - gameWindowWidth);
        const uint32_t y = arrangeSideBySide ? 0 : (screenHeight - gameWindowHeight - 48);
        SetWindowPos(aThisWindow, nullptr, x, y, 0, 0, SWP_NOSIZE);
    }
}
