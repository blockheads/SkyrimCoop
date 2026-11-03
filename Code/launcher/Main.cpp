#include <windows.h>
#include "../client/RuntimeVersion.h"
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

// Installed as the game folder's SkyrimSELauncher.exe (docs/install.md): Steam's Play button and invites start it.
namespace
{
constexpr const wchar_t* cTitle = L"SkyrimCoop";
// The package's plugins, in load order: Alternate Start needs USSEP before it; our start last.
constexpr std::array<const char*, 3> cPlugins{"unofficial skyrim special edition patch.esp",
                                               "alternate start - live another life.esp", "SkyrimCoopStart.esp"};

void Fail(const std::wstring& acMessage)
{
    MessageBoxW(nullptr, acMessage.c_str(), cTitle, MB_OK | MB_ICONERROR);
}

// SkyrimSE.exe's file version; empty if unreadable.
std::wstring GameVersion(const std::filesystem::path& acExe)
{
    DWORD handle = 0;
    const DWORD cSize = GetFileVersionInfoSizeW(acExe.c_str(), &handle);
    if (!cSize)
        return {};
    std::vector<char> data(cSize);
    VS_FIXEDFILEINFO* pInfo = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(acExe.c_str(), 0, cSize, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&pInfo), &length) || !pInfo)
        return {};
    return std::format(L"{}.{}.{}.{}", HIWORD(pInfo->dwFileVersionMS), LOWORD(pInfo->dwFileVersionMS),
                       HIWORD(pInfo->dwFileVersionLS), LOWORD(pInfo->dwFileVersionLS));
}

std::filesystem::path KnownFolder(REFKNOWNFOLDERID aId)
{
    PWSTR pPath = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(aId, 0, nullptr, &pPath)))
        path = pPath;
    CoTaskMemFree(pPath);
    return path;
}

// Bethesda's launcher writes the INIs on a first run; this one replaces it: the game's defaults if missing.
void EnsureIni(const std::filesystem::path& acGame)
{
    const auto cMyGames = KnownFolder(FOLDERID_Documents) / L"My Games" / L"Skyrim Special Edition";
    std::error_code error;
    std::filesystem::create_directories(cMyGames, error);
    for (const auto& [cName, cDefault] : {std::pair{L"Skyrim.ini", L"Skyrim_Default.ini"}, std::pair{L"SkyrimPrefs.ini", L"High.ini"}})
        if (!std::filesystem::exists(cMyGames / cName))
            std::filesystem::copy_file(acGame / cDefault, cMyGames / cName, error);
}

// Plugins.txt (the game's active plugins): the package's enabled ("*" in front), last and in their order.
bool EnablePlugins()
{
    const auto cFolder = KnownFolder(FOLDERID_LocalAppData) / L"Skyrim Special Edition";
    const auto cFile = cFolder / L"Plugins.txt";
    std::vector<std::string> lines;
    {
        std::ifstream in(cFile);
        for (std::string line; std::getline(in, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const std::string cName = line.starts_with('*') ? line.substr(1) : line;
            if (std::ranges::none_of(cPlugins, [&](const char* acpPlugin) { return _stricmp(cName.c_str(), acpPlugin) == 0; }))
                lines.push_back(line);
        }
    }
    for (const char* cpPlugin : cPlugins)
        lines.push_back(std::string("*") + cpPlugin);
    std::error_code error;
    std::filesystem::create_directories(cFolder, error);
    std::ofstream out(cFile, std::ios::trunc | std::ios::binary);
    for (const auto& cLine : lines)
        out << cLine << "\r\n";
    return static_cast<bool>(out);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    std::array<wchar_t, 32768> module{};
    const DWORD count = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!count || count >= module.size())
        return 1;
    const std::filesystem::path cGame = std::filesystem::path(std::wstring(module.data(), count)).parent_path();

    // SKSE and our plugin must match this build's selected runtime.
    const std::wstring cVersion = GameVersion(cGame / L"SkyrimSE.exe");
    const std::wstring cRequired(SkyrimRuntime::Name.begin(), SkyrimRuntime::Name.end());
    if (cVersion != cRequired)
    {
        Fail(std::format(L"This SkyrimCoop build requires Skyrim Special Edition {}; this game is {}.\n\n"
                         L"Install the SkyrimCoop build matching your game version.",
                         cRequired, cVersion.empty() ? L"unknown" : cVersion));
        return 1;
    }
    EnsureIni(cGame);
    if (!EnablePlugins())
        Fail(L"Couldn't enable SkyrimCoop's plugins in Plugins.txt: the game would start without them.");

    // Steam passes +connect_lobby to its launcher. SKSE starts Skyrim with a null command line, so carry
    // the original arguments in this process's environment; Skyrim inherits them through SKSE.
    if (!SetEnvironmentVariableW(L"SKYRIMCOOP_LAUNCH_ARGS", GetCommandLineW()))
    {
        Fail(L"Couldn't preserve the Steam invite. Try launching SkyrimCoop again.");
        return 1;
    }
    const std::wstring cLoader = (cGame / L"skse64_loader.exe").wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(cLoader.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, cGame.c_str(), &startup, &process))
    {
        Fail(L"Couldn't start SKSE (skse64_loader.exe). Extract the whole SkyrimCoop zip into the game folder, then try again.");
        return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    return static_cast<int>(result);
}
