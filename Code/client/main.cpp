
#include <Plugin.h>
#include <windows.h>
#include <shellapi.h>
#include <Hooks/FunctionHook.h>
#include <VersionDb.h>
#include <Diagnostics/Profile.h>

std::unique_ptr<Plugin> g_plugin{nullptr};

static void ShowAddressLibraryError(const wchar_t* apGamePath)
{
    const auto cMessage = std::format(L"Failed to load the matching Skyrim Address Library (\"All in One\").\n"
                                      L"Looking for it here: {}\\Data\\SKSE\\Plugins\n\nOpen its Nexus page?",
                                      apGamePath);
    if (MessageBoxW(nullptr, cMessage.c_str(), L"SkyrimCoop", MB_YESNO | MB_ICONERROR) == IDYES)
        ShellExecuteW(nullptr, L"open", LR"(https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)", nullptr, nullptr, SW_SHOWNORMAL);
    exit(4);
}

void InitPlugin(const std::filesystem::path& acGamePath, const std::string& aExeVersion)
{
    if (!VersionDb::Get().Load(acGamePath, aExeVersion))
    {
        ShowAddressLibraryError(acGamePath.c_str());
    }

    g_plugin = std::make_unique<Plugin>();

    Profile::Start(); // before any hook of ours runs
    Plugin::InstallHooks();
    HOOK_COMMIT;
}

void StartPlugin()
{
    g_plugin->Start();
}
