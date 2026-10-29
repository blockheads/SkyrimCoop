#include <windows.h>

#include <array>
#include <string>

// Steam passes +connect_lobby to its launcher. SKSE starts Skyrim with a null command line, so carry
// the original arguments in this process's environment; Skyrim inherits them through SKSE.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    std::array<wchar_t, 32768> module{};
    const DWORD count = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!count || count >= module.size())
        return 1;
    std::wstring directory(module.data(), count);
    const auto slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return 1;
    directory.resize(slash);
    const std::wstring loader = directory + L"\\skse64_loader.exe";
    if (!SetEnvironmentVariableW(L"SKYRIMCOOP_LAUNCH_ARGS", GetCommandLineW()))
    {
        MessageBoxW(nullptr, L"Couldn't preserve the Steam invite. Try launching SkyrimCoop again.", L"SkyrimCoop",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(loader.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup,
                        &process))
    {
        MessageBoxW(nullptr,
                    L"Couldn't start SKSE. Install skse64_loader.exe beside the SkyrimCoop launcher, then try again.",
                    L"SkyrimCoop", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    return static_cast<int>(result);
}
