
#include <Plugin.h>
#include <ClientPch.h>

#include <windows.h>
#include <shellapi.h>

std::unique_ptr<Plugin> g_plugin{nullptr};

static void ShowAddressLibraryError(const wchar_t* apGamePath)
{
    const auto cMessage = fmt::format(L"Failed to load the Skyrim Address Library (\"All in one (1.6.X)\").\n"
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

    Plugin::InstallHooks();
    HOOK_COMMIT;
}

void StartPlugin()
{
    g_plugin->Start();
}
