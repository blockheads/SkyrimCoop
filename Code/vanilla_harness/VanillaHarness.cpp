// The test harness without SkyrimCoop: an SKSE plugin that only runs the harness's console
// commands and reports a heartbeat, so `scripts/coop_test.py repro --vanilla` runs a crash recipe
// exactly as with our plugin (same commands, same frame, same camera) and answers "is it us?".
//
// It speaks the subset of the client's control files the harness falls back to without the
// command socket (Code/client/HotReload/Runtime.cpp): boot.txt (one command once the main menu is
// up), console.txt ("<id> <command>", or "<id> @show Menu" / "@hide Menu"), and status.json
// (heartbeat_ms, pid, console_id, session.world_ready). Nothing else of ours is loaded.
//
// Addresses are for Skyrim SE 1.6.1170, the only version the harness runs (Address Library IDs
// in the comments; scripts/re/build_re_data.py resolves them).
#include <windows.h>

#include <MinHook.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
constexpr uintptr_t cMainUpdate = 0x645ea0;      // 36564 Main::Update
constexpr uintptr_t cExecuteCommand = 0x8f3f30;  // 442672 Console::ExecuteCommand(const char*)
constexpr uintptr_t cUISingleton = 0x20f6a00;    // 400327 UI*
constexpr uintptr_t cIsMenuOpen = 0xfa37b0;      // 82074 UI::IsMenuOpen(const BSFixedString&)
constexpr uintptr_t cFixedString = 0xcec5d0;     // 69161 BSFixedString::BSFixedString(const char*)
constexpr uintptr_t cPlayerSingleton = 0x3137698; // 401069 PlayerCharacter*
constexpr uintptr_t cAddMessage = 0x1af260;      // 13631 UIMessageQueue::AddMessage
constexpr uintptr_t cMessageQueue = 0x20f8950;   // 400445 UIMessageQueue*
constexpr size_t cParentCell = 0x60;             // TESObjectREFR::parentCell (CommonLibSSE)
constexpr uint32_t cShow = 1, cHide = 3;         // UI_MESSAGE_TYPE

struct BSFixedString
{
    const char* Data = nullptr;
};

uintptr_t s_base = 0;
template <class T> T At(uintptr_t aRva)
{
    return reinterpret_cast<T>(s_base + aRva);
}

std::filesystem::path s_control;
std::string s_bootCommand;
bool s_bootDone = false;
std::string s_consoleId;
std::chrono::steady_clock::time_point s_nextStatus{};

using TMainUpdate = short(void*);
TMainUpdate* s_realMainUpdate = nullptr;

void ExecuteCommand(const char* apLine)
{
    At<void (*)(const char*)>(cExecuteCommand)(apLine);
}

BSFixedString Fixed(const char* apText)
{
    BSFixedString string;
    At<void (*)(BSFixedString*, const char*)>(cFixedString)(&string, apText);
    return string;
}

bool IsMenuOpen(const char* apMenu)
{
    void* pUI = *At<void**>(cUISingleton);
    if (!pUI)
        return false;
    const BSFixedString cName = Fixed(apMenu);
    return At<bool (*)(void*, const BSFixedString&)>(cIsMenuOpen)(pUI, cName);
}

void SetMenuShown(const char* apMenu, bool aShown)
{
    void* pQueue = *At<void**>(cMessageQueue);
    if (!pQueue)
        return;
    const BSFixedString cName = Fixed(apMenu);
    At<void (*)(void*, const BSFixedString*, uint32_t, void*)>(cAddMessage)(pQueue, &cName, aShown ? cShow : cHide, nullptr);
}

bool InWorld()
{
    const auto* pPlayer = *At<const uint8_t**>(cPlayerSingleton);
    return pPlayer && *reinterpret_cast<void* const*>(pPlayer + cParentCell);
}

void Run(const std::string& acLine)
{
    if (acLine.rfind("@show ", 0) == 0 || acLine.rfind("@hide ", 0) == 0)
        SetMenuShown(acLine.c_str() + 6, acLine[1] == 's');
    else
        ExecuteCommand(acLine.c_str());
}

void WriteStatus()
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < s_nextStatus)
        return;
    s_nextStatus = cNow + std::chrono::milliseconds(250);
    const auto cEpochMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto cTemporary = s_control / "status.json.tmp";
    {
        std::ofstream file(cTemporary, std::ios::trunc);
        file << "{\"vanilla\": true, \"heartbeat_ms\": " << cEpochMs << ", \"pid\": " << GetCurrentProcessId()
             << ", \"console_id\": \"" << s_consoleId << "\", \"session\": {\"world_ready\": "
             << (s_bootDone && InWorld() ? "true" : "false") << "}}";
    }
    MoveFileExW(cTemporary.c_str(), (s_control / "status.json").c_str(), MOVEFILE_REPLACE_EXISTING);
}

void Poll()
{
    if (!s_bootDone)
    {
        if (s_bootCommand.empty())
        {
            std::ifstream boot(s_control / "boot.txt");
            std::getline(boot, s_bootCommand);
        }
        if (!s_bootCommand.empty() && IsMenuOpen("Main Menu"))
        {
            ExecuteCommand(s_bootCommand.c_str());
            s_bootDone = true;
        }
    }
    else
    {
        std::ifstream console(s_control / "console.txt");
        std::string line;
        if (console && std::getline(console, line))
        {
            const auto cSeparator = line.find(' ');
            if (cSeparator != std::string::npos && line.compare(0, cSeparator, s_consoleId) != 0)
            {
                s_consoleId = line.substr(0, cSeparator);
                Run(line.substr(cSeparator + 1));
            }
        }
    }
    WriteStatus();
}

short HookMainUpdate(void* apMain)
{
    Poll();
    return s_realMainUpdate(apMain);
}

HMODULE s_self = nullptr;
} // namespace

struct SKSEPluginVersionData
{
    uint32_t dataVersion;
    uint32_t pluginVersion;
    char name[256];
    char author[256];
    char supportEmail[252];
    uint32_t versionIndependenceEx;
    uint32_t versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
};
static_assert(offsetof(SKSEPluginVersionData, author) == 0x108 && offsetof(SKSEPluginVersionData, supportEmail) == 0x208,
              "SKSE's PluginVersionData layout (CommonLibSSE)");

// As the client's (Code/client/skse_entry.cpp): no struct use, Address Library and post-629
// structs, built for 1.6.1170.
extern "C" __declspec(dllexport) const SKSEPluginVersionData SKSEPlugin_Version = {
    1, 1, "SkyrimCoopVanillaHarness", "SkyrimCoop", "", 1 << 0, (1 << 0) | (1 << 2),
    {(1u << 24) | (6u << 16) | (1170u << 4), 0}, 0};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const void*)
{
    s_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(s_self, path, MAX_PATH);
    const char* pInstance = std::getenv("SKYRIMCOOP_INSTANCE");
    s_control = std::filesystem::path(path).parent_path() /
                (std::string("SkyrimCoopHotReload") + (pInstance && *pInstance ? std::string("-") + pInstance : ""));
    std::error_code error;
    std::filesystem::create_directories(s_control, error);

    if (MH_Initialize() != MH_OK)
        return false;
    void* pTarget = At<void*>(cMainUpdate);
    if (MH_CreateHook(pTarget, reinterpret_cast<void*>(&HookMainUpdate), reinterpret_cast<void**>(&s_realMainUpdate)) != MH_OK)
        return false;
    return MH_EnableHook(pTarget) == MH_OK;
}

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID)
{
    if (aReason == DLL_PROCESS_ATTACH)
        s_self = aInstance;
    return TRUE;
}
