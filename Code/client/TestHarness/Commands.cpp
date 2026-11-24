#include "Commands.h"
#include "ActivateWatch.h"
#include "ClipLoads.h"
#include "Harness.h"
#include "JsonReply.h"
#include <Diagnostics/Counters.h>

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

CommandServer::Reply StartCommand(Harness& aHarness, const std::string& acRequest)
{
    const auto cReady = [](const nlohmann::json& acReply) { return CommandServer::Reply{acReply.dump(), {}}; };
    const auto cRequest = nlohmann::json::parse(acRequest, nullptr, false);
    if (cRequest.is_discarded() || !cRequest.is_object())
        return cReady(Error("not a JSON object"));
    // nlohmann throws on a wrongly typed field (a hex string for an id): refuse the request, never take the game down.
    try
    {
        const std::string cName = cRequest.value("cmd", "");
        // Any command with "at" waits for that moment on this game's clock (Scheduled).
        if (cName == "clock")
            return cReady(nlohmann::json{{"ok", true}, {"now_ms", Scheduled::NowMs()}, {"frame", aHarness.Schedule.Frame}});
        if (cName == "scheduled")
            return cReady(aHarness.Schedule.Take(cRequest.value("id", 0u)));
        if (cRequest.contains("at"))
            return cReady(nlohmann::json{{"ok", true}, {"queued", aHarness.Schedule.Queue(cRequest, cRequest["at"].get<double>())}});
        for (const auto cCommands : {StagingCommands(), InputCommands(), WorldCommands(), KnowledgeCommands(), PlayerStateCommands(), CrimeCommands(), SessionCommands(), MemoryCommands(), ChangeLogCommands(), PlayerEventsCommands(), ActivateWatch::Commands(), ClipLoads::Commands(), QuestFragmentsCommands(), SamplerCommands(), ActionCommands()})
            for (const Command& cCommand : cCommands)
            {
                if (cCommand.Name != cName)
                    continue;
                if (cCommand.NeedsWorld && !aHarness.Files.InWorld())
                    return cReady(Error("not in the world yet"));
                // Each command's game-thread time: `profile` lists the harness's own cost.
                const ::Diagnostics::Counters::ScopedTime cTime(::Diagnostics::Counters::Get(("harness " + cName + " us").c_str()));
                if (cCommand.Gather)
                    return {{}, [build = cCommand.Gather(aHarness, cRequest)] { return DumpReply(build); }};
                return cReady(cCommand.Run(aHarness, cRequest));
            }
        return cReady(Error("unknown cmd " + cName));
    }
    catch (const nlohmann::json::exception& acError)
    {
        return cReady(Error(std::string("bad request: ") + acError.what()));
    }
}

std::string HandleCommand(Harness& aHarness, const std::string& acRequest)
{
    auto reply = StartCommand(aHarness, acRequest);
    return reply.Later ? reply.Later() : std::move(reply.Ready);
}
} // namespace TestHarness
