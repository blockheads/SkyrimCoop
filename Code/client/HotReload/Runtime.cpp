#include <TiltedOnlinePCH.h>
#include "Runtime.h"
#include "Session.h"
#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Games/Skyrim/Interface/UI.h>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace CoopReload
{
std::string InstanceSuffix()
{
    // SKYRIMCOOP_INSTANCE=client keeps two games in one install apart (control files, logs).
    char instance[32]{};
    const DWORD length = GetEnvironmentVariableA("SKYRIMCOOP_INSTANCE", instance, sizeof(instance));
    if (length == 0 || length >= sizeof(instance))
        return {};
    const std::string name(instance);
    const bool valid = std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
    return valid ? "-" + name : std::string{};
}

Runtime& Runtime::Get()
{
    // Process-lifetime. Unload from EndMain, never from CRT destructors/DllMain.
    static Runtime* runtime = new Runtime;
    return *runtime;
}

void Runtime::Configure(const std::filesystem::path& pluginDirectory)
{
    m_directory = pluginDirectory / ("SkyrimCoopHotReload" + InstanceSuffix());
}

// Console::ExecuteCommand(const char*): logs, compiles and runs the line like the console.
static void ExecuteConsoleCommand(const char* apCommand)
{
    using TExecuteCommand = void(const char*);
    POINTER_SKYRIMSE(TExecuteCommand, s_executeCommand, 442672);
    s_executeCommand.Get()(apCommand);
}

// boot.txt holds one console command (e.g. `coc QASmoke`) run once the main menu is up.
// The engine only runs the INI's sStartingConsoleCommand on shutdown in 1.6.1170.
void Runtime::PollBootCommand()
{
    if (m_bootState == "none")
    {
        std::ifstream bootFile(m_directory / "boot.txt");
        if (!bootFile || !std::getline(bootFile, m_bootCommand) || m_bootCommand.empty())
            return;
        m_bootState = "waiting_for_main_menu";
    }
    if (m_bootState != "waiting_for_main_menu")
        return;

    const auto* pUI = UI::Get();
    if (!pUI || !pUI->GetMenuOpen("Main Menu"))
        return;

    spdlog::info("Boot command: {}", m_bootCommand);
    ExecuteConsoleCommand(m_bootCommand.c_str());
    m_bootState = "executed";
}

// console.txt: "<id> <command>"; each new id runs once (scripts/coop_test.py console).
void Runtime::PollConsoleCommand()
{
    std::ifstream consoleFile(m_directory / "console.txt");
    std::string line;
    if (!consoleFile || !std::getline(consoleFile, line))
        return;
    const auto separator = line.find(' ');
    if (separator == std::string::npos || line.compare(0, separator, m_consoleId) == 0)
        return;
    m_consoleId = line.substr(0, separator);
    spdlog::info("Console command {}: {}", m_consoleId, line.substr(separator + 1));
    ExecuteConsoleCommand(line.c_str() + separator + 1);
}

static nlohmann::json PlayerStatus()
{
    auto& actors = World::Get().GetActorBridge();
    if (!actors.ActorHas3D(0x14))
        return nullptr;
    float position[3]{}, rotation[3]{};
    actors.GetActorPosition(0x14, position[0], position[1], position[2]);
    actors.GetActorRotation(0x14, rotation[0], rotation[1], rotation[2]);
    return {{"position", {position[0], position[1], position[2]}}, {"rotation", {rotation[0], rotation[1], rotation[2]}}};
}

void Runtime::Poll()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_directory.empty() || now < m_nextPoll) return;
    m_nextPoll = now + std::chrono::milliseconds(250);
    PollBootCommand();
    if (m_bootState == "executed")
        PollConsoleCommand();
    std::ifstream requestFile(m_directory / "request.txt");
    if (!requestFile) return; // Opt-in by publishing a module.
    char buffer[128]{};
    requestFile.getline(buffer, sizeof(buffer));
    const std::string request(buffer);
    if (request != m_request)
    {
        m_request = request;
        const bool valid = !requestFile.fail() && !request.empty() && request.size() < 100 &&
            request.find("..") == std::string::npos &&
            std::all_of(request.begin(), request.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            });
        if (request == "builtin")
        {
            m_loader.Unload();
            m_state = "builtin";
            m_error.clear();
            m_observations.clear();
        }
        else if (valid && m_loader.Reload(m_directory / request, m_error))
        {
            m_state = "loaded";
            m_observations.clear();
            spdlog::info("Hot reload: loaded {} (generation {})", m_loader.BuildId(), m_loader.Generation());
        }
        else
        {
            if (!valid) m_error = "invalid module filename";
            m_state = "rejected";
            spdlog::error("Hot reload rejected {}: {}. Previous movement code remains active.", request, m_error);
        }
    }

    const auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto actors = nlohmann::json::object();
    for (const auto& [id, observation] : m_observations)
        actors[std::to_string(id)] = {
            {"observed_ms", observation.observedMs}, {"generation", observation.generation},
            {"has_3d", observation.has3D},
            {"requested", {observation.requested[0], observation.requested[1], observation.requested[2]}},
            {"actual", {observation.actual[0], observation.actual[1], observation.actual[2]}}
        };
    nlohmann::json status = {
        {"pid", GetCurrentProcessId()}, {"heartbeat_ms", unixMs},
        {"request", m_request}, {"state", m_state}, {"error", m_error},
        {"build_id", m_loader.BuildId()}, {"generation", m_loader.Generation()},
        {"evaluations", m_loader.Calls()}, {"invalid_outputs", m_loader.InvalidOutputs()},
        {"actors", actors}, {"session", PollSession(m_directory)},
        {"boot", {{"command", m_bootCommand}, {"state", m_bootState}}},
        {"console_id", m_consoleId}, {"player", PlayerStatus()}
    };
    const auto temporary = m_directory / "status.json.tmp";
    const auto destination = m_directory / "status.json";
    std::ofstream output(temporary, std::ios::trunc);
    output << status.dump(2) << '\n';
    output.close();
    if (output)
        MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void Runtime::Shutdown() { m_loader.Unload(); }
bool Runtime::Evaluate(const CoopMovementInput& input, CoopMovementOutput& output)
{
    return m_loader.Evaluate(input, output);
}
void Runtime::Observe(uint32_t formId, const CoopMovementOutput& requested,
                      const float actual[3], bool has3D)
{
    if (m_directory.empty() || m_request.empty()) return;
    const auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (m_observations.size() >= 128 && m_observations.find(formId) == m_observations.end())
        m_observations.erase(m_observations.begin());
    // Read back from Actor in the engine, never from the interpolation cache.
    m_observations[formId] = {unixMs, m_loader.Generation(), has3D,
        {requested.position[0], requested.position[1], requested.position[2]},
        {actual[0], actual[1], actual[2]}};
}
}
