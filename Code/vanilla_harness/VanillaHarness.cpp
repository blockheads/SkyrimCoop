// The test harness without SkyrimCoop: an SKSE plugin that only runs the harness's console
// commands and reports a heartbeat, so `scripts/coop_test.py repro --vanilla` runs a crash recipe
// exactly as with our plugin (same commands, same frame, same camera) and answers "is it us?".
//
// It speaks the subset of the client's control files the harness falls back to without the
// command socket (Code/client/TestHarness/ControlFiles.h): boot.txt (one command once the main menu is
// up), console.txt ("<id> <command>", "<id> @show Menu" / "@hide Menu", or "<id> @on <form> <command>" to run it on
// that reference), and status.json
// (heartbeat_ms, pid, console_id, session.world_ready, and with watch.txt an actor's place). Nothing else of ours is loaded.
//
// Engine addresses come from the selected runtime's Address Library.
#include <windows.h>
#include "../client/VersionDb.h"

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
constexpr uint32_t cMainUpdate = 36564;
constexpr uint32_t cExecuteCommand = 442672;
constexpr uint32_t cUISingleton = 400327;
constexpr uint32_t cIsMenuOpen = 82074;
constexpr uint32_t cFixedString = 69161;
constexpr uint32_t cPlayerSingleton = 401069;
constexpr uint32_t cAddMessage = 13631;
constexpr uint32_t cMessageQueue = 400445;
constexpr uint32_t cFormById = 14617; // TESForm::GetById
constexpr uint32_t cCompileAndRun = 441582; // Script::CompileAndRun_Impl
constexpr size_t cScriptText = 0x38;        // Script::text (Forms/Script.h)
constexpr size_t cParentCell = 0x60;             // TESObjectREFR::parentCell (CommonLibSSE)
constexpr size_t cAngle = 0x48, cLocation = 0x54; // TESObjectREFR::data.angle, .location (CommonLibSSE)
constexpr uint32_t cShow = 1, cHide = 3;         // UI_MESSAGE_TYPE

struct BSFixedString
{
    const char* Data = nullptr;
};

uintptr_t s_base = 0;
template <class T> T At(uintptr_t aRva)
{
    return reinterpret_cast<T>(VersionDb::Get().FindAddressById(aRva));
}

std::filesystem::path s_control;
std::string s_bootCommand;
bool s_bootDone = false;
std::string s_consoleId;
std::chrono::steady_clock::time_point s_nextStatus{};
bool s_tracing = false; // player_trace.on exists: the player's place each frame into player_trace.txt

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

// A line that names its reference: the console queues it, so the target waits here until its text runs.
using TCompileAndRun = void(void* apScript, void* apCompiler, uint32_t aCompilerName, void* apTarget);
TCompileAndRun* s_realCompileAndRun = nullptr;
std::string s_targetLine;
uint32_t s_targetForm = 0;

void HookCompileAndRun(void* apScript, void* apCompiler, uint32_t aCompilerName, void* apTarget)
{
    const char* pText = *reinterpret_cast<const char**>(static_cast<uint8_t*>(apScript) + cScriptText);
    if (s_targetForm && pText && s_targetLine == pText)
    {
        apTarget = At<void* (*)(uint32_t)>(cFormById)(s_targetForm);
        s_targetForm = 0;
    }
    s_realCompileAndRun(apScript, apCompiler, aCompilerName, apTarget);
}

void Run(const std::string& acLine)
{
    if (acLine.rfind("@show ", 0) == 0 || acLine.rfind("@hide ", 0) == 0)
        SetMenuShown(acLine.c_str() + 6, acLine[1] == 's');
    else if (const auto cSpace = acLine.find(' ', 4); acLine.rfind("@on ", 0) == 0 && cSpace != std::string::npos)
    {
        s_targetForm = std::stoul(acLine.substr(4, cSpace - 4), nullptr, 16);
        s_targetLine = acLine.substr(cSpace + 1);
        ExecuteCommand(s_targetLine.c_str());
    }
    else
        ExecuteCommand(acLine.c_str());
}

// watch.txt holds a form id in hex: its place and the player's go in the status, to compare an NPC's movement.
void WriteWatched(std::ofstream& aFile)
{
    std::ifstream watch(s_control / "watch.txt");
    uint32_t formId = 0;
    const auto* pPlayer = *At<const uint8_t**>(cPlayerSingleton);
    if (!(watch >> std::hex >> formId) || !pPlayer)
        return;
    const auto* pActor = At<const uint8_t* (*)(uint32_t)>(cFormById)(formId);
    if (!pActor)
        return;
    const auto* pAt = reinterpret_cast<const float*>(pActor + cLocation);
    const auto* pMe = reinterpret_cast<const float*>(pPlayer + cLocation);
    aFile << ", \"watched\": [" << pAt[0] << ", " << pAt[1] << ", " << pAt[2] << "], \"player\": [" << pMe[0] << ", "
          << pMe[1] << ", " << pMe[2] << "]";
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
             << (s_bootDone && InWorld() ? "true" : "false") << "}";
        WriteWatched(file);
        file << "}";
    }
    MoveFileExW(cTemporary.c_str(), (s_control / "status.json").c_str(), MOVEFILE_REPLACE_EXISTING);
    s_tracing = std::filesystem::exists(s_control / "player_trace.on");
}

// The player's place and heading this frame, wall-clock stamped (as the client's animation trace): what the
// strafe scenarios compare a co-op player's movement against (S10).
void TracePlayer()
{
    const auto* pPlayer = *At<const uint8_t**>(cPlayerSingleton);
    if (!s_tracing || !pPlayer)
        return;
    const auto* pAngle = reinterpret_cast<const float*>(pPlayer + cAngle);
    const auto* pLocation = reinterpret_cast<const float*>(pPlayer + cLocation);
    const auto cEpochUs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream file(s_control / "player_trace.txt", std::ios::app);
    file << cEpochUs << ' ' << pLocation[0] << ' ' << pLocation[1] << ' ' << pLocation[2] << ' ' << pAngle[2] << '\n';
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
    TracePlayer();
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

// Exact runtime compatibility, as the client's (Code/client/skse_entry.cpp).
extern "C" __declspec(dllexport) const SKSEPluginVersionData SKSEPlugin_Version = {
    1, 1, "SkyrimCoopVanillaHarness", "SkyrimCoop", "", 1 << 1, 0,
    {SkyrimRuntime::PackedVersion, 0}, 0};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const void*)
{
    s_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(s_self, path, MAX_PATH);
    const auto game = std::filesystem::path(path).parent_path().parent_path().parent_path().parent_path();
    if (!VersionDb::Get().Load(game, std::string(SkyrimRuntime::Name)))
        return false;
    for (uint32_t id : {cFormById, cCompileAndRun, cMainUpdate, cExecuteCommand, cUISingleton, cIsMenuOpen, cFixedString, cPlayerSingleton, cAddMessage, cMessageQueue})
        if (!VersionDb::Get().FindAddressById(id)) return false;
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
    void* pCompileAndRun = At<void*>(cCompileAndRun);
    if (MH_CreateHook(pCompileAndRun, reinterpret_cast<void*>(&HookCompileAndRun), reinterpret_cast<void**>(&s_realCompileAndRun)) != MH_OK)
        return false;
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID)
{
    if (aReason == DLL_PROCESS_ATTACH)
        s_self = aInstance;
    return TRUE;
}
