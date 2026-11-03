#include <windows.h>
#include "LaunchChoice.h"
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
// The settings process inherits Steam's environment and Proton prefix.
HANDLE OpenGraphics(const std::filesystem::path& acGame)
{
    const auto cLauncher = acGame / L"SkyrimSELauncher.bethesda.exe";
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(cLauncher, fileError))
    {
        Fail(L"Bethesda's launcher hasn't been preserved yet.\n\n"
             L"Restore it with Steam's Verify integrity, rename SkyrimSELauncher.exe to "
             L"SkyrimSELauncher.bethesda.exe, then extract the SkyrimCoop zip again.");
        return nullptr;
    }
    std::array<wchar_t, 32768> self{};
    const DWORD cSelfLength = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    std::error_code error;
    if (!cSelfLength || cSelfLength >= self.size())
        return nullptr;
    const std::filesystem::path cSelf(std::wstring(self.data(), cSelfLength));
    const auto cBackupSize = std::filesystem::file_size(cLauncher, error);
    if (!error && cBackupSize == std::filesystem::file_size(cSelf, error) && !error)
    {
        std::ifstream backup(cLauncher, std::ios::binary), current(cSelf, std::ios::binary);
        if (std::equal(std::istreambuf_iterator<char>(backup), {}, std::istreambuf_iterator<char>(current), {}))
        {
            Fail(L"The saved launcher is SkyrimCoop's launcher. Restore Bethesda's launcher with Steam's Verify integrity, "
                 L"then preserve that executable before reinstalling SkyrimCoop.");
            return nullptr;
        }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(cLauncher.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, acGame.c_str(), &startup, &process))
    {
        Fail(L"Couldn't open Bethesda's graphics settings launcher.");
        return nullptr;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}

struct Menu
{
    const std::filesystem::path& Game;
    HANDLE Settings{};
    bool Play{};
};

LRESULT CALLBACK MenuProc(HWND aWindow, UINT aMessage, WPARAM aWParam, LPARAM aLParam)
{
    auto* pMenu = reinterpret_cast<Menu*>(GetWindowLongPtrW(aWindow, GWLP_USERDATA));
    if (aMessage == WM_NCCREATE)
    {
        pMenu = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(aLParam)->lpCreateParams);
        SetWindowLongPtrW(aWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pMenu));
    }
    if (aMessage == WM_COMMAND && pMenu)
    {
        switch (LOWORD(aWParam))
        {
        case 1:
            pMenu->Play = true;
            DestroyWindow(aWindow);
            return 0;
        case 2:
            if (!pMenu->Settings)
            {
                pMenu->Settings = OpenGraphics(pMenu->Game);
                if (pMenu->Settings)
                {
                    EnableWindow(GetDlgItem(aWindow, 1), FALSE);
                    EnableWindow(GetDlgItem(aWindow, 2), FALSE);
                    SetTimer(aWindow, 1, 200, nullptr);
                }
            }
            return 0;
        }
    }
    if (aMessage == WM_TIMER && pMenu && pMenu->Settings && WaitForSingleObject(pMenu->Settings, 0) == WAIT_OBJECT_0)
    {
        CloseHandle(pMenu->Settings);
        pMenu->Settings = nullptr;
        KillTimer(aWindow, 1);
        EnableWindow(GetDlgItem(aWindow, 1), TRUE);
        EnableWindow(GetDlgItem(aWindow, 2), TRUE);
        SetForegroundWindow(aWindow);
    }
    if (aMessage == WM_DESTROY)
    {
        if (pMenu && pMenu->Settings)
            CloseHandle(pMenu->Settings);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(aWindow, aMessage, aWParam, aLParam);
}

bool ShowMenu(HINSTANCE aInstance, const std::filesystem::path& acGame)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = MenuProc;
    windowClass.hInstance = aInstance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"SkyrimCoopLauncher";
    if (!RegisterClassW(&windowClass))
        return false;
    Menu menu{acGame};
    RECT rect{0, 0, 460, 225};
    AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    const int cWidth = rect.right - rect.left, cHeight = rect.bottom - rect.top;
    HWND window = CreateWindowW(windowClass.lpszClassName, cTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN) - cWidth) / 2, (GetSystemMetrics(SM_CYSCREEN) - cHeight) / 2,
        cWidth, cHeight, nullptr, nullptr, aInstance, &menu);
    if (!window)
        return false;
    HFONT font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const auto control = [&](const wchar_t* acpClass, const wchar_t* acpText, DWORD aStyle,
                             int aX, int aY, int aWidth, int aHeight, int aId) {
        HWND child = CreateWindowW(acpClass, acpText, WS_CHILD | WS_VISIBLE | aStyle, aX, aY, aWidth, aHeight,
                                   window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(aId)), aInstance, nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return child;
    };
    control(L"STATIC", L"Ready to play SkyrimCoop", 0, 24, 20, 412, 30, 0);
    HWND play = control(L"BUTTON", L"Play co-op", WS_TABSTOP | BS_DEFPUSHBUTTON, 24, 63, 412, 42, 1);
    control(L"BUTTON", L"Graphics settings", WS_TABSTOP, 24, 117, 412, 38, 2);
    control(L"STATIC", L"In Bethesda's launcher, choose Options, then Exit.\nReturn here and choose Play co-op.", 0, 24, 170, 412, 44, 0);
    ShowWindow(window, SW_SHOW);
    SetFocus(play);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DeleteObject(font);
    return menu.Play;
}
} // namespace

int WINAPI wWinMain(HINSTANCE aInstance, HINSTANCE, PWSTR, int)
{
    std::array<wchar_t, 32768> module{};
    const DWORD count = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!count || count >= module.size())
        return 1;
    const std::filesystem::path cGame = std::filesystem::path(std::wstring(module.data(), count)).parent_path();

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    std::vector<std::wstring_view> views;
    for (int i = 1; arguments && i < argumentCount; ++i)
        views.emplace_back(arguments[i]);
    const auto cChoice = Launcher::Select(views);
    LocalFree(arguments);
    if (cChoice == Launcher::Choice::Graphics)
    {
        HANDLE settings = OpenGraphics(cGame);
        if (!settings)
            return 1;
        WaitForSingleObject(settings, INFINITE);
        CloseHandle(settings);
        return 0;
    }
    if (cChoice == Launcher::Choice::Menu && !ShowMenu(aInstance, cGame))
        return 0;

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
    {
        Fail(L"Couldn't enable SkyrimCoop's plugins in Plugins.txt: the game would start without them.");
        return 1;
    }

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
