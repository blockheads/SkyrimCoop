#include <ClientPch.h>
#include "ConsoleOutput.h"

#include <Games/Skyrim/Forms/Script.h>
#include <Games/Skyrim/Interface/ConsoleLog.h>
#include <TESObjectREFR.h>

#include <deque>
#include <map>

namespace TestHarness::ConsoleOutput
{
namespace
{
struct Ticket
{
    uint64_t Id;
    std::string Line;
    uint32_t Target;
    std::vector<std::string> Output;
};

// Game thread only: lines are queued, run and read there.
std::deque<Ticket> s_waiting;
std::map<uint64_t, std::vector<std::string>> s_ran;
uint64_t s_next = 1;
bool s_running = false;
thread_local Ticket* t_pCurrent = nullptr;

using TVPrint = void(ConsoleLog*, const char* apFormat, va_list aArguments);
TVPrint* RealVPrint = nullptr;
using TRunQueued = void();
TRunQueued* RealRunQueued = nullptr;
using TCompileAndRun = void(Script*, void* apCompiler, uint32_t aCompilerName, void* apTarget);
TCompileAndRun* RealCompileAndRun = nullptr;

void HookVPrint(ConsoleLog* apThis, const char* apFormat, va_list aArguments)
{
    RealVPrint(apThis, apFormat, aArguments);
    std::string line(apThis->lastMessage, strnlen(apThis->lastMessage, sizeof(apThis->lastMessage)));
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    if (t_pCurrent)
        t_pCurrent->Output.push_back(std::move(line));
}

void HookRunQueued()
{
    s_running = true;
    RealRunQueued();
    s_running = false;
}

// Each queued line runs here; the first waiting ticket with its text takes what it prints.
void HookCompileAndRun(Script* apThis, void* apCompiler, uint32_t aCompilerName, void* apTarget)
{
    const char* pText = apThis->text ? apThis->text : "";
    const auto cTicket = s_running ? std::ranges::find_if(s_waiting, [pText](const Ticket& acTicket) { return _stricmp(acTicket.Line.c_str(), pText) == 0; })
                                   : s_waiting.end();
    if (cTicket == s_waiting.end())
    {
        RealCompileAndRun(apThis, apCompiler, aCompilerName, apTarget);
        return;
    }
    Ticket ticket = std::move(*cTicket);
    s_waiting.erase(cTicket);
    void* pTarget = apTarget;
    if (ticket.Target)
        pTarget = Cast<TESObjectREFR>(TESForm::GetById(ticket.Target));
    if (!pTarget && ticket.Target)
        ticket.Output.push_back(fmt::format("Reference {:X} not found (harness target): not run.", ticket.Target));
    else
    {
        t_pCurrent = &ticket;
        RealCompileAndRun(apThis, apCompiler, aCompilerName, pTarget);
        t_pCurrent = nullptr;
    }
    if (!ticket.Output.empty())
        spdlog::info("Console '{}' printed: {}", ticket.Line, fmt::join(ticket.Output, " | "));
    s_ran[ticket.Id] = std::move(ticket.Output);
    while (s_ran.size() > 256)
        s_ran.erase(s_ran.begin());
}

Hooks::Initializer s_consoleOutputHooks(
    []()
    {
        POINTER_SKYRIMSE(TVPrint, s_vprint, 51110);                 // ConsoleLog::VPrint
        POINTER_SKYRIMSE(TRunQueued, s_runQueued, 442673);          // Console::RunQueuedCommands
        POINTER_SKYRIMSE(TCompileAndRun, s_compileAndRun, 441582);  // Script::CompileAndRun_Impl
        RealVPrint = s_vprint.Get();
        RealRunQueued = s_runQueued.Get();
        RealCompileAndRun = s_compileAndRun.Get();
        HOOK(&RealVPrint, HookVPrint);
        HOOK(&RealRunQueued, HookRunQueued);
        HOOK(&RealCompileAndRun, HookCompileAndRun);
    });
} // namespace

uint64_t Expect(const std::string& acLine, uint32_t aTargetFormId)
{
    s_waiting.push_back({s_next, acLine, aTargetFormId, {}});
    return s_next++;
}

std::optional<std::vector<std::string>> Take(uint64_t aTicket)
{
    const auto cRan = s_ran.find(aTicket);
    if (cRan == s_ran.end())
        return std::nullopt;
    auto output = std::move(cRan->second);
    s_ran.erase(cRan);
    return output;
}
} // namespace TestHarness::ConsoleOutput
