#pragma once
#include "../../hotreload/MovementLoader.h"
#include "CommandServer.h"
#include <chrono>
#include <map>

namespace CoopReload
{
// "-<name>" from SKYRIMCOOP_INSTANCE, or empty: suffix for per-instance control files and logs.
std::string InstanceSuffix();

class Runtime
{
public:
    static Runtime& Get();
    void Configure(const std::filesystem::path& pluginDirectory);
    // Game update thread only, before dispatching service events.
    void Poll();
    void Shutdown();
    bool Evaluate(const CoopMovementInput& input, CoopMovementOutput& output);
    void Observe(uint32_t formId, const CoopMovementOutput& requested,
                 const float actual[3], bool has3D);
private:
    void PollBootCommand();
    void PollConsoleCommand();
    // The status JSON (status.json and the command socket's "status").
    std::string BuildStatus(int aIndent = -1);
    std::string HandleCommand(const std::string& acRequest);

    CommandServer m_commands;
    bool m_commandsStarted = false;

    MovementLoader m_loader;
    std::string m_bootCommand;
    std::string m_bootState = "none";
    std::string m_consoleRequest; // "<id> <command>" from console.txt
    std::string m_consoleId;      // id of the last executed request

    std::filesystem::path m_directory;
    std::string m_request;
    std::string m_error;
    std::string m_state = "builtin";
    std::chrono::steady_clock::time_point m_nextPoll{};
    struct Observation
    {
        int64_t observedMs;
        uint64_t generation;
        bool has3D;
        float requested[3];
        float actual[3];
        float requestedRotation[3];
    };
    std::map<uint32_t, Observation> m_observations;
};
}
