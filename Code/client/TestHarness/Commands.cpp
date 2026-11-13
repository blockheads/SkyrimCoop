#include "Commands.h"
#include "Harness.h"

namespace TestHarness
{
nlohmann::json Ok()
{
    return {{"ok", true}};
}

nlohmann::json Error(std::string_view aMessage)
{
    return {{"ok", false}, {"error", aMessage}};
}

std::string HandleCommand(Harness& aHarness, const std::string& acRequest)
{
    const auto cRequest = nlohmann::json::parse(acRequest, nullptr, false);
    if (cRequest.is_discarded() || !cRequest.is_object())
        return Error("not a JSON object").dump();
    // nlohmann throws on a wrongly typed field (a hex string for an id): refuse the request, never take the game down.
    try
    {
        const std::string cName = cRequest.value("cmd", "");
        // Any command with "at" waits for that moment on this game's clock (Scheduled).
        if (cName == "clock")
            return nlohmann::json{{"ok", true}, {"now_ms", Scheduled::NowMs()}, {"frame", aHarness.Schedule.Frame}}.dump();
        if (cName == "scheduled")
            return aHarness.Schedule.Take(cRequest.value("id", 0u)).dump();
        if (cRequest.contains("at"))
            return nlohmann::json{{"ok", true}, {"queued", aHarness.Schedule.Queue(cRequest, cRequest["at"].get<double>())}}.dump();
        for (const auto cCommands : {StagingCommands(), InputCommands(), WorldCommands(), KnowledgeCommands(), CrimeCommands(), SessionCommands(), MemoryCommands(), ChangeLogCommands(), PlayerEventsCommands(), QuestFragmentsCommands(), SamplerCommands(), ActionCommands()})
            for (const Command& cCommand : cCommands)
            {
                if (cCommand.Name != cName)
                    continue;
                if (cCommand.NeedsWorld && !aHarness.Files.InWorld())
                    return Error("not in the world yet").dump();
                return cCommand.Run(aHarness, cRequest).dump();
            }
        return Error("unknown cmd " + cName).dump();
    }
    catch (const nlohmann::json::exception& acError)
    {
        return Error(std::string("bad request: ") + acError.what()).dump();
    }
}
} // namespace TestHarness
