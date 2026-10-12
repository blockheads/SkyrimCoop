#include <TiltedOnlinePCH.h>

#include <TiltedOnlineApp.h>
#include <Breadcrumbs.h>
#include <VersionDb.h>

#include <deque>
#include <FunctionHook.hpp>

#include <DInputHook.hpp>
#include <WindowsHook.hpp>

#include <World.h>
#include <PluginPaths.h>
#if COOP_TEST_HARNESS
#include <TestHarness/TestHarness.h>
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef HAS_DIRECTXTK
#include <Systems/RenderSystemD3D11.h>
#endif

#include <Services/OverlayService.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>

#include <ScriptExtender.h>
#ifdef HAS_DIRECTXTK
#include <NvidiaUtil.h>
#endif

using TiltedPhoques::Debug;

TiltedOnlineApp::TiltedOnlineApp()
{
    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    auto logPath = PluginPaths::GetDirectory() / "logs";

    std::error_code ec;
    create_directory(logPath, ec);

    auto rotatingLogger = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath / ("tp_client" + PluginPaths::InstanceSuffix() + ".log"), 1048576 * 5, 3);
    // rotatingLogger->set_level(spdlog::level::debug);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("%^[%H:%M:%S.%e] [%l] [tid %t] %$ %v");

    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console, rotatingLogger});
    set_default_logger(logger);
    // e.g. SKYRIMCOOP_LOG_LEVEL=debug (set by scripts/coop_test.py)
    char level[16]{};
    if (GetEnvironmentVariableA("SKYRIMCOOP_LOG_LEVEL", level, sizeof(level)) > 0)
        logger->set_level(spdlog::level::from_str(level));
    // Readable while the game runs (test harness, hangs); errors land before a crash dump.
    logger->flush_on(spdlog::level::warn);
    spdlog::flush_every(std::chrono::seconds(1));
}

TiltedOnlineApp::~TiltedOnlineApp() = default;

void* TiltedOnlineApp::GetMainAddress() const
{
    POINTER_SKYRIMSE(void, winMain, 36544);

    return winMain.GetPtr();
}

bool TiltedOnlineApp::BeginMain()
{
    World::Create();
#ifdef HAS_DISCORD
    World::Get().ctx().at<DiscordService>().Init();
#endif
#ifdef HAS_DIRECTXTK
    World::Get().ctx().emplace<RenderSystemD3D11>(World::Get().ctx().at<OverlayService>(), World::Get().ctx().at<ImguiService>());
#endif

    LoadScriptExender();

#ifdef HAS_DIRECTXTK
    // TODO: Figure out a way to un-blacklist NvCamera64.dll (see DllBlocklist.cpp). Then this hack can be removed
    if (IsNvidiaOverlayLoaded())
        ApplyNvidiaFix();
#endif

    return true;
}

bool TiltedOnlineApp::EndMain()
{
#if COOP_TEST_HARNESS
    TestHarness::Shutdown();
#endif
    UninstallHooks();
#ifdef HAS_DIRECTXTK
    if (m_pDevice)
        m_pDevice->Release();
#endif

    return true;
}

void TiltedOnlineApp::Update()
{
    // Reverting a change that used to be here to disable bUseFaceGenPreprocessedHeads==true (which is
    // the default) handling. Extensive testing over months by multiple parties showed that enabling
    // the flag introduces no issues WITH PROPERLY GENERATED CHARACTERS (in-game character generation
    // or showracemenu). The shortcut of  "coc riverwood" from the main menu skips proper character generation.
    //
    // Plus, having it on  has some benefits like helping with neck seams. Comment to avoid revisiting.
    //
    // There are still some issues to track down, like hair color and maybe face tint not syncing correctly,
    // but they are unrelated and unchanged by this flag.
    //

    // Make sure the window stays active
    POINTER_SKYRIMSE(uint32_t, bAlwaysActive, 380768);

    *bAlwaysActive = 1;

    World::Get().Update();
}

bool TiltedOnlineApp::Attach()
{
    TiltedPhoques::Debug::OnAttach();

    // TiltedPhoques::Nop(0x1405D3FA1, 6);
    return true;
}

bool TiltedOnlineApp::Detach()
{
    TiltedPhoques::Debug::OnDetach();
    return true;
}

// SKYRIMCOOP_PROBE=13408,36559: engine functions (Address Library IDs) whose calls leave crash
// breadcrumbs ("probe:13408": calls, when, thread, first argument), to see whether and in what order
// the engine ran them before a crash. No code change per investigation.
static void InstallProbes()
{
    const char* pProbes = std::getenv("SKYRIMCOOP_PROBE");
    if (!pProbes)
        return;
    static std::deque<std::string> s_names; // breadcrumbs keep the pointers
    for (const char* p = pProbes; *p;)
    {
        char* pEnd = nullptr;
        const auto cId = static_cast<uint32_t>(std::strtoul(p, &pEnd, 10));
        if (pEnd == p)
            break;
        p = *pEnd == ',' ? pEnd + 1 : pEnd;
        VersionDbPtr<void> function(cId);
        const std::string& cName = s_names.emplace_back("probe:" + std::to_string(cId));
        if (function.GetPtr() && TiltedPhoques::FunctionHookManager::GetInstance().AddProbe(function.GetPtr(), cName.c_str()))
            spdlog::warn("Probe on engine function {}", cName);
        else
            spdlog::error("Probe on engine function {}: could not hook it (unknown ID, or hooked already)", cName);
    }
}

void TiltedOnlineApp::InstallHooks2()
{
    // Before any hook is added: each is wrapped so that its calls leave crash breadcrumbs.
    TiltedPhoques::SetHookObserver(&Breadcrumbs::RecordHook, &Breadcrumbs::ObservesHook);
    TiltedPhoques::Initializer::RunAll();
    InstallProbes();

    TiltedPhoques::DInputHook::Install();
}

void TiltedOnlineApp::UninstallHooks()
{
}

#ifdef HAS_DIRECTXTK
void TiltedOnlineApp::ApplyNvidiaFix() noexcept
{
    auto d3dFeatureLevelOut = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = CreateEarlyDxDevice(&m_pDevice, &d3dFeatureLevelOut);
    if (FAILED(hr))
        spdlog::error("D3D11CreateDevice failed. Detected an NVIDIA GPU, error code={0:x}", hr);

    if (d3dFeatureLevelOut < D3D_FEATURE_LEVEL_11_0)
        spdlog::warn("Unexpected D3D11 feature level detected (< 11.0), may cause issues");
}
#endif
