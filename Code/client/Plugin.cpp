#include <Plugin.h>
#include <Breadcrumbs.h>
#include <VersionDb.h>

#include <deque>
#include <Hooks/FunctionHook.h>

#include <Hooks/DInputHook.h>

#include <World.h>
#include <PluginPaths.h>
#include <Diagnostics/Folders.h>
#include <Diagnostics/LogRing.h>
#if COOP_TEST_HARNESS
#include <TestHarness/TestHarness.h>
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Systems/RenderSystemD3D11.h>

#include <Services/ImguiService.h>

#include <NvidiaUtil.h>
#include <Hooks/Initializer.h>

Plugin::Plugin()
{
    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    auto logPath = PluginPaths::GetDirectory() / "logs";

    // The last run's logs as it left them, before this run's log opens: a crash or freeze becomes a report.
    // Not in test games (coop_test.py), which are killed after every run.
    const auto cMarks = Diagnostics::Folders::RunMarks();
    const bool cReports = GetEnvironmentVariableA("SKYRIMCOOP_NO_CRASH_REPORTS", nullptr, 0) == 0;
    const auto cLastRun = cReports ? Diagnostics::CrashReports::CollectLastRun(cMarks, Diagnostics::Folders::Sources(), PluginPaths::InstanceSuffix())
                                   : std::nullopt;
    if (cReports)
        Diagnostics::CrashReports::MarkRunning(cMarks);

    std::error_code ec;
    create_directory(logPath, ec);

    auto rotatingLogger = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath / ("tp_client" + PluginPaths::InstanceSuffix() + ".log"), 1048576 * 5, 3);
    // rotatingLogger->set_level(spdlog::level::debug);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("%^[%H:%M:%S.%e] [%l] [tid %t] %$ %v");

    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console, rotatingLogger, Diagnostics::MakePluginLogSink()});
    set_default_logger(logger);
    // e.g. SKYRIMCOOP_LOG_LEVEL=debug (set by scripts/coop_test.py)
    char level[16]{};
    if (GetEnvironmentVariableA("SKYRIMCOOP_LOG_LEVEL", level, sizeof(level)) > 0)
        logger->set_level(spdlog::level::from_str(level));
    // Readable while the game runs (test harness, hangs); errors land before a crash dump.
    logger->flush_on(spdlog::level::warn);
    spdlog::flush_every(std::chrono::seconds(1));

    // After a crash the zip to send is ready without starting a game (docs/design/bug-reports.md).
    if (cLastRun)
    {
        spdlog::warn("The last run did not end cleanly: {}", cLastRun->string());
        std::thread([] { Diagnostics::Folders::ExportNow(""); }).detach();
    }
}

Plugin::~Plugin() = default;

void Plugin::Start()
{
    World::Create();
    World::Get().ctx().emplace<RenderSystemD3D11>(World::Get().ctx().at<ImguiService>());

    // TODO: Figure out a way to un-blacklist NvCamera64.dll (see DllBlocklist.cpp). Then this hack can be removed
    if (IsNvidiaOverlayLoaded())
        ApplyNvidiaFix();
}

void Plugin::Update()
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
        if (function.GetPtr() && Hooks::FunctionHookManager::GetInstance().AddProbe(function.GetPtr(), cName.c_str()))
            spdlog::warn("Probe on engine function {}", cName);
        else
            spdlog::error("Probe on engine function {}: could not hook it (unknown ID, or hooked already)", cName);
    }
}

void Plugin::InstallHooks()
{
    // Before any hook is added: each is wrapped so that its calls leave crash breadcrumbs.
    Hooks::SetHookObserver(&Breadcrumbs::RecordHook, &Breadcrumbs::ObservesHook);
    Hooks::Initializer::RunAll();
    InstallProbes();

    Hooks::DInputHook::Install();
}

void Plugin::ApplyNvidiaFix() noexcept
{
    auto d3dFeatureLevelOut = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = CreateEarlyDxDevice(&m_pDevice, &d3dFeatureLevelOut);
    if (FAILED(hr))
        spdlog::error("D3D11CreateDevice failed. Detected an NVIDIA GPU, error code={0:x}", hr);

    if (d3dFeatureLevelOut < D3D_FEATURE_LEVEL_11_0)
        spdlog::warn("Unexpected D3D11 feature level detected (< 11.0), may cause issues");
}
