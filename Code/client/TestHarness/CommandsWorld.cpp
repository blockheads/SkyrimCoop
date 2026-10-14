#include <TiltedOnlinePCH.h>
#include "Commands.h"
#include "EngineCalls.h"
#include "Harness.h"
#include "Status.h"

#include <World.h>
#include <Bridges/IFormBridge.h>

// Reading the world, and the player acting on its objects.
namespace TestHarness
{
namespace
{
// {"cmd": "status"}: the status (Status.h).
nlohmann::json GetStatus(Harness& aHarness, const nlohmann::json&)
{
    return {{"ok", true}, {"status", BuildStatus(aHarness)}};
}

// {"cmd": "graph_bool", "actor": id, "names": ["IsAttackReady", ...]}: the actor's behavior graph
// booleans by name (false for one its graph lacks).
nlohmann::json GraphBool(Harness&, const nlohmann::json& acRequest)
{
    auto values = nlohmann::json::object();
    for (const auto& cName : acRequest.value("names", std::vector<std::string>{}))
        values[cName] = EngineCalls::GetGraphBool(acRequest.value("actor", 0x14u), cName.c_str());
    return {{"ok", true}, {"values", values}};
}

// {"cmd": "kill_move_check", "attacker": id, "victim": id}: what decides a kill move (EngineCalls::CheckKillMove).
nlohmann::json KillMoveCheckCommand(Harness&, const nlohmann::json& acRequest)
{
    const auto cCheck = EngineCalls::CheckKillMove(acRequest.value("attacker", 0x14u), acRequest.value("victim", 0u));
    return {{"ok", true},
            {"should_attack_kill", cCheck.ShouldAttackKill},
            {"killable", cCheck.Killable},
            {"engaging_in_combat", cCheck.EngagingInCombat},
            {"in_kill_move", cCheck.InKillMove},
            {"crosshair_actor", cCheck.CrosshairActor},
            {"attack_target", cCheck.AttackTarget}};
}

// {"cmd": "idle_conditions", "idle": id, "subject": id, "target": id}: the idle and its parents with their
// events and whether each one's list holds (EngineCalls::CheckIdleTree), and each condition evaluated alone
// (EngineCalls::CheckIdleConditions).
nlohmann::json IdleConditions(Harness&, const nlohmann::json& acRequest)
{
    auto idles = nlohmann::json::array();
    for (const auto& cNode : EngineCalls::CheckIdleTree(acRequest.value("idle", 0u), acRequest.value("subject", 0x14u),
                                                        acRequest.value("target", 0u)))
        idles.push_back({{"idle", cNode.Idle}, {"event", cNode.Event}, {"passes", cNode.Passes}});
    auto conditions = nlohmann::json::array();
    for (const auto& cCondition : EngineCalls::CheckIdleConditions(
             acRequest.value("idle", 0u), acRequest.value("subject", 0x14u), acRequest.value("target", 0u)))
        conditions.push_back({{"idle", cCondition.Idle},
                              {"function", cCondition.Function},
                              {"op", cCondition.OpCode},
                              {"or", cCondition.Or},
                              {"run_on", cCondition.RunOn},
                              {"comparison", cCondition.Comparison},
                              {"true", cCondition.True}});
    return {{"ok", true}, {"idles", idles}, {"conditions", conditions}};
}

// {"cmd": "anim_trace", "actors": [id, ...], "bools": ["bIsSynced", ...]}: starts recording these actors'
// animations (AnimationTrace), or adds them; {"cmd": "anim_trace"} alone takes what was recorded so far, and
// {"cmd": "anim_trace", "stop": true} stops and takes the rest.
nlohmann::json AnimTrace(Harness& aHarness, const nlohmann::json& acRequest)
{
    if (acRequest.value("stop", false))
        return {{"ok", true}, {"entries", aHarness.Trace.Stop()}};
    if (acRequest.contains("actors"))
        aHarness.Trace.Watch(acRequest.value("actors", std::vector<uint32_t>{}), acRequest.value("bools", std::vector<std::string>{}));
    return {{"ok", true}, {"entries", aHarness.Trace.Take()}};
}

// {"cmd": "graph_state", "actor": id}: the actor's graph state, hex, and the save's microseconds.
nlohmann::json GraphState(Harness&, const nlohmann::json& acRequest)
{
    int64_t micros = 0;
    const std::string cState = EngineCalls::SaveGraphState(acRequest.value("actor", 0x14u), micros);
    if (cState.empty())
        return Error("no graph state");
    static constexpr char cDigits[] = "0123456789abcdef";
    std::string hex(cState.size() * 2, '0');
    for (size_t i = 0; i < cState.size(); ++i)
    {
        hex[2 * i] = cDigits[uint8_t(cState[i]) >> 4];
        hex[2 * i + 1] = cDigits[uint8_t(cState[i]) & 0xF];
    }
    return {{"ok", true}, {"state", hex}, {"micros", micros}};
}

// {"cmd": "load_graph_state", "actor": id, "state": hex}: loads it into the actor now.
nlohmann::json LoadGraphState(Harness&, const nlohmann::json& acRequest)
{
    const std::string cHex = acRequest.value("state", std::string{});
    std::string state(cHex.size() / 2, '\0');
    for (size_t i = 0; i < state.size(); ++i)
        state[i] = char(std::stoi(cHex.substr(2 * i, 2), nullptr, 16));
    int64_t micros = 0;
    const bool cLoaded = EngineCalls::LoadGraphState(acRequest.value("actor", 0u), state, micros);
    return {{"ok", cLoaded}, {"micros", micros}};
}

// {"cmd": "graph_writers", "actor": id}: the variables the actor's graphs write themselves, by node.
nlohmann::json GraphWriters(Harness&, const nlohmann::json& acRequest)
{
    auto writers = nlohmann::json::array();
    for (const auto& cWriter : EngineCalls::GetGraphWriters(acRequest.value("actor", 0x14u)))
        writers.push_back({{"project", cWriter.Project}, {"variable", cWriter.Variable}, {"project_name", cWriter.ProjectName},
                           {"behaviour", cWriter.Behaviour}, {"node", cWriter.Node}, {"index", cWriter.Index}});
    return {{"ok", true}, {"writers", writers}};
}

// {"cmd": "objects"}: the doors, containers and furniture in the player's cell (state, lock, load door, position).
nlohmann::json Objects(Harness&, const nlohmann::json&)
{
    constexpr uint8_t cDoor = 29;      // FormType::Door
    constexpr uint8_t cFurniture = 40; // FormType::Furniture
    auto& forms = World::Get().GetFormBridge();
    auto objects = nlohmann::json::array();
    for (const auto& cObject : EngineCalls::GetCellObjects())
    {
        const uint32_t cId = cObject.Id;
        const bool cIsDoor = forms.IsObjectBaseFormType(cId, cDoor);
        const bool cHasLock = forms.ObjectHasLock(cId);
        // open_state: TESObjectREFR::OpenState, 1 open, 2 opening, 3 closed, 4 closing.
        objects.push_back(
            {{"id", cId},
             {"door", cIsDoor},
             {"furniture", forms.IsObjectBaseFormType(cId, cFurniture)},
             {"name", forms.GetFormName(cId)},
             {"load_door", forms.IsLoadDoor(cId)},
             {"position", {cObject.Position[0], cObject.Position[1], cObject.Position[2]}},
             {"open_state", cIsDoor ? forms.GetObjectOpenState(cId) : 0},
             {"has_lock", cHasLock},
             {"locked", cHasLock && forms.ObjectIsLocked(cId)},
             {"lock_level", cHasLock ? forms.GetObjectLockLevel(cId) : 0}});
    }
    return {{"ok", true}, {"objects", objects}};
}

// {"cmd": "activate", "object": id}: the player activates it (as pressing E on it).
nlohmann::json Activate(Harness&, const nlohmann::json& acRequest)
{
    auto& forms = World::Get().GetFormBridge();
    const uint32_t cObject = acRequest.value("object", 0u);
    if (!forms.FormExists(cObject))
        return Error("no such object");
    spdlog::info("Activate (rpc): {:X}", cObject);
    EngineCalls::PlayerActivates(cObject);
    return Ok();
}

// {"cmd": "lock", "object": id, "locked": true, "level": 50}: changes its lock as the game does (the
// lock-change hook sees it).
nlohmann::json Lock(Harness&, const nlohmann::json& acRequest)
{
    auto& forms = World::Get().GetFormBridge();
    const uint32_t cObject = acRequest.value("object", 0u);
    if (!forms.FormExists(cObject))
        return Error("no such object");
    spdlog::info("Lock (rpc): {:X} {}", cObject, acRequest.value("locked", true));
    EngineCalls::ChangeLockAsGame(cObject, acRequest.value("locked", true), uint8_t(acRequest.value("level", 0u)));
    return Ok();
}

constexpr Command cCommands[] = {
    {"status", false, &GetStatus},
    {"graph_bool", true, &GraphBool},
    {"kill_move_check", true, &KillMoveCheckCommand},
    {"anim_trace", true, &AnimTrace},
    {"idle_conditions", true, &IdleConditions},
    {"objects", true, &Objects},
    {"activate", true, &Activate},
    {"lock", true, &Lock},
    {"graph_state", true, &GraphState},
    {"load_graph_state", true, &LoadGraphState},
    {"graph_writers", true, &GraphWriters},
};
} // namespace

std::span<const Command> WorldCommands()
{
    return cCommands;
}
} // namespace TestHarness
