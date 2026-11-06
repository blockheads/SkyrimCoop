// The test harness without SkyrimCoop: an SKSE plugin that only runs the harness's console
// commands and reports a heartbeat, so `scripts/coop_test.py repro --vanilla` runs a crash recipe
// exactly as with our plugin (same commands, same frame, same camera) and answers "is it us?".
//
// It speaks the subset of the client's control files the harness falls back to without the
// command socket (Code/client/TestHarness/ControlFiles.h): boot.txt (one command once the main menu is
// up), console.txt ("<id> <command>", or "<id> @show Menu" / "@hide Menu"), console_out.txt (the id, then what
// the console printed for that line), and status.json
// (heartbeat_ms, pid, console_id, session.world_ready). Nothing else of ours is loaded.
//
// Engine addresses come from the selected runtime's Address Library.
#include <windows.h>
#include "../client/VersionDb.h"

#include <MinHook.h>

#include <chrono>
#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
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
constexpr uint32_t cConsoleVPrint = 51110; // ConsoleLog::VPrint
constexpr uint32_t cCompileAndRun = 441582; // Script::CompileAndRun_Impl
constexpr uint32_t cFormById = 14617;       // TESForm::GetById
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

// Frame times as the client's World::Update counts them (kStallMs 100), for `coop_test.py profile` against vanilla:
// one bucket per ms below 1 s, per 100 ms below 30 s, the last for longer frames (profiling.py, histogram_percentiles).
constexpr size_t cFrameBuckets = 1000 + 290 + 1;
constexpr double cStallMs = 100.0;
std::chrono::steady_clock::time_point s_lastFrame{};
uint64_t s_frames = 0, s_stalls = 0;
double s_longestFrameMs = 0.0;
uint64_t s_frameHistogram[cFrameBuckets]{};

using TMainUpdate = short(void*);
TMainUpdate* s_realMainUpdate = nullptr;

// What the console prints while Run executes a line (the command runs at once, 442672).
using TVPrint = void(void*, const char*, va_list);
TVPrint* s_realVPrint = nullptr;
bool s_capturing = false, s_printedChanged = false;
std::string s_printed;

void HookVPrint(void* apThis, const char* apFormat, va_list aArguments)
{
    if (s_capturing && apFormat)
    {
        va_list copy;
        va_copy(copy, aArguments);
        char line[1024];
        std::vsnprintf(line, sizeof(line), apFormat, copy);
        va_end(copy);
        s_printed += line;
        s_printed += '\n';
        s_printedChanged = true;
    }
    s_realVPrint(apThis, apFormat, aArguments);
}

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

// "<form id>.<command>" runs on that reference, as the client's harness target (ConsoleOutput.cpp).
using TCompileAndRun = void(void*, void*, uint32_t, void*);
TCompileAndRun* s_realCompileAndRun = nullptr;
void* s_pTarget = nullptr;

void HookCompileAndRun(void* apScript, void* apCompiler, uint32_t aName, void* apTarget)
{
    void* pTarget = s_pTarget ? s_pTarget : apTarget;
    s_pTarget = nullptr; // the queued line runs in a later frame (Console::RunQueuedCommands 442673)
    s_realCompileAndRun(apScript, apCompiler, aName, pTarget);
}

void Run(const std::string& acLine)
{
    if (acLine.rfind("@show ", 0) == 0 || acLine.rfind("@hide ", 0) == 0)
    {
        SetMenuShown(acLine.c_str() + 6, acLine[1] == 's');
        return;
    }
    const auto cDot = acLine.find('.');
    if (cDot != std::string::npos && cDot > 0 && cDot <= 8 &&
        acLine.find_first_not_of("0123456789abcdefABCDEF") == cDot)
    {
        const uint32_t cId = std::stoul(acLine.substr(0, cDot), nullptr, 16);
        s_pTarget = At<void* (*)(uint32_t)>(cFormById)(cId);
        if (s_pTarget)
            ExecuteCommand(acLine.c_str() + cDot + 1);
        else
            s_printed += "Reference not found (harness target): not run.\n";
        return;
    }
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
             << (s_bootDone && InWorld() ? "true" : "false") << "}, \"frames\": {\"frames\": " << s_frames
             << ", \"stalls\": " << s_stalls << ", \"longest_frame_ms\": " << s_longestFrameMs << ", \"histogram_ms\": [";
        for (size_t i = 0; i < cFrameBuckets; ++i)
            file << (i ? "," : "") << s_frameHistogram[i];
        file << "]}}";
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
                s_printed.clear();
                s_capturing = true; // until the next line: results can print in later frames
                Run(line.substr(cSeparator + 1));
                s_printedChanged = true;
                s_nextStatus = {};
            }
        }
    }
    if (s_printedChanged)
    {
        s_printedChanged = false;
        const auto cTemporary = s_control / "console_out.txt.tmp";
        {
            std::ofstream out(cTemporary, std::ios::trunc);
            out << s_consoleId << '\n' << s_printed;
        }
        MoveFileExW(cTemporary.c_str(), (s_control / "console_out.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    WriteStatus();
    TracePlayer();
}

void CountFrame()
{
    const auto cNow = std::chrono::steady_clock::now();
    if (s_lastFrame.time_since_epoch().count() != 0)
    {
        const double cMs = std::chrono::duration<double, std::milli>(cNow - s_lastFrame).count();
        ++s_frames;
        s_stalls += cMs > cStallMs;
        s_longestFrameMs = cMs > s_longestFrameMs ? cMs : s_longestFrameMs;
        const size_t cBucket = cMs < 1000.0 ? static_cast<size_t>(cMs) : 1000 + static_cast<size_t>((cMs - 1000.0) / 100.0);
        ++s_frameHistogram[cBucket < cFrameBuckets - 1 ? cBucket : cFrameBuckets - 1];
    }
    s_lastFrame = cNow;
}

short HookMainUpdate(void* apMain)
{
    CountFrame();
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
    for (uint32_t id : {cMainUpdate, cExecuteCommand, cUISingleton, cIsMenuOpen, cFixedString, cPlayerSingleton, cAddMessage, cMessageQueue, cConsoleVPrint, cCompileAndRun, cFormById})
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
    void* pPrint = At<void*>(cConsoleVPrint);
    if (MH_CreateHook(pPrint, reinterpret_cast<void*>(&HookVPrint), reinterpret_cast<void**>(&s_realVPrint)) != MH_OK)
        return false;
    void* pCompile = At<void*>(cCompileAndRun);
    if (MH_CreateHook(pCompile, reinterpret_cast<void*>(&HookCompileAndRun), reinterpret_cast<void**>(&s_realCompileAndRun)) != MH_OK)
        return false;
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID)
{
    if (aReason == DLL_PROCESS_ATTACH)
        s_self = aInstance;
    return TRUE;
}
