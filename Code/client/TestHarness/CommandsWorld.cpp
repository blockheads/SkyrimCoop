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

std::string ToHex(const std::string& acBytes)
{
    static constexpr char cDigits[] = "0123456789abcdef";
    std::string hex(acBytes.size() * 2, '0');
    for (size_t i = 0; i < acBytes.size(); ++i)
    {
        hex[2 * i] = cDigits[uint8_t(acBytes[i]) >> 4];
        hex[2 * i + 1] = cDigits[uint8_t(acBytes[i]) & 0xF];
    }
    return hex;
}

std::string FromHex(const std::string& acHex)
{
    std::string bytes(acHex.size() / 2, '\0');
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = char(std::stoi(acHex.substr(2 * i, 2), nullptr, 16));
    return bytes;
}

// {"cmd": "graph_state", "actor": id}: the actor's graph state, hex, and the save's microseconds.
nlohmann::json GraphState(Harness&, const nlohmann::json& acRequest)
{
    int64_t micros = 0;
    const std::string cState = EngineCalls::SaveGraphState(acRequest.value("actor", 0x14u), micros);
    if (cState.empty())
        return Error("no graph state");
    return {{"ok", true}, {"state", ToHex(cState)}, {"micros", micros}};
}

// {"cmd": "load_graph_state", "actor": id, "state": hex}: loads it into the actor now.
nlohmann::json LoadGraphState(Harness&, const nlohmann::json& acRequest)
{
    int64_t micros = 0;
    const bool cLoaded = EngineCalls::LoadGraphState(acRequest.value("actor", 0u), FromHex(acRequest.value("state", std::string{})), micros);
    return {{"ok", cLoaded}, {"micros", micros}};
}

// {"cmd": "form_save", "form": id, "flags": n, "base": false}: what the form (with "base", the reference's base
// form) would write to a save for those change flags (default: its recorded changes), hex (FormState::Save).
nlohmann::json FormSave(Harness&, const nlohmann::json& acRequest)
{
    const std::optional<uint32_t> cFlags =
        acRequest.contains("flags") ? std::optional<uint32_t>(acRequest["flags"].get<uint32_t>()) : std::nullopt;
    const auto cSaved = EngineCalls::SaveFormState(acRequest.value("form", 0u), acRequest.value("base", false), cFlags);
    if (!cSaved)
        return Error("no such form, or a save or load runs");
    return {{"ok", true}, {"form", cSaved->FormId}, {"flags", cSaved->ChangeFlags}, {"data", ToHex(cSaved->Data)}};
}

// {"cmd": "form_load", "form": id, "flags": n, "data": hex}: loads it into the form now (FormState::Load).
nlohmann::json FormLoad(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::LoadFormState(acRequest.value("form", 0u), acRequest.value("flags", 0u),
                                    FromHex(acRequest.value("data", std::string{}))))
        return Error("no such form, or a save or load runs");
    spdlog::info("Form state loaded (rpc): {:X}", acRequest.value("form", 0u));
    return Ok();
}

// {"cmd": "engine_events", "since": n}: the engine's script events numbered n and later (EngineEvents).
nlohmann::json GetEngineEvents(Harness& aHarness, const nlohmann::json& acRequest)
{
    return aHarness.Events.Since(acRequest.value("since", uint64_t(0)));
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

// {"cmd": "quest", "quest": id}: the quest's live state (EngineCalls::GetQuestState), to check what a save holds.
nlohmann::json Quest(Harness&, const nlohmann::json& acRequest)
{
    const auto cState = EngineCalls::GetQuestState(acRequest.value("quest", 0u));
    if (!cState)
        return Error("no such quest");
    auto aliases = nlohmann::json::array();
    for (const auto& cAlias : cState->Aliases)
        aliases.push_back({{"id", cAlias.Id}, {"name", cAlias.Name}, {"ref", cAlias.Ref}, {"base", cAlias.Base}});
    return {{"ok", true},
            {"flags", cState->Flags},
            {"current_stage", cState->CurrentStage},
            {"already_run", cState->AlreadyRun},
            {"stages", cState->Stages},
            {"objectives", cState->Objectives},
            {"aliases", aliases}};
}

// {"cmd": "script_state", "object": id, "script": name}: the state that script on the object is in.
nlohmann::json ScriptState(Harness&, const nlohmann::json& acRequest)
{
    const auto cState = EngineCalls::GetScriptState(acRequest.value("object", 0u), acRequest.value("script", std::string()).c_str());
    if (!cState)
        return Error("no such script on that object");
    return {{"ok", true}, {"state", *cState}};
}

// {"cmd": "script_stacks"}: the VM's running stacks, each its frames innermost first ("Script.Function").
nlohmann::json ScriptStacks(Harness&, const nlohmann::json&)
{
    auto stacks = nlohmann::json::array();
    for (const auto& cStack : EngineCalls::GetRunningScripts())
        stacks.push_back({{"id", cStack.Id}, {"frames", cStack.Frames}});
    return {{"ok", true}, {"stacks", stacks}};
}

// {"cmd": "script_var", "object": id, "script": name, "var": name, "value"?: number}: a script variable or
// auto property (its "::<Name>_var"), set first when a value is given.
nlohmann::json ScriptVar(Harness&, const nlohmann::json& acRequest)
{
    const auto cObject = acRequest.value("object", 0u);
    const auto cScript = acRequest.value("script", std::string());
    auto name = acRequest.value("var", std::string());
    if (!EngineCalls::GetScriptVariable(cObject, cScript.c_str(), name.c_str()))
        name = "::" + name + "_var";
    if (acRequest.contains("value") && !EngineCalls::SetScriptVariable(cObject, cScript.c_str(), name.c_str(), acRequest["value"].get<double>()))
        return Error("no such int, float or bool variable");
    const auto cValue = EngineCalls::GetScriptVariable(cObject, cScript.c_str(), name.c_str());
    if (!cValue)
        return Error("no such script on that object, or no such variable");
    nlohmann::json value = nullptr;
    switch (cValue->Type)
    {
    case 2: value = cValue->String; break;
    case 3: value = cValue->Int; break;
    case 4: value = cValue->Float; break;
    case 5: value = cValue->Bool; break;
    default: value = cValue->Form;
    }
    return {{"ok", true}, {"name", name}, {"type", cValue->Type}, {"value", value}};
}

// {"cmd": "item_count", "object": id, "item": id}: how many of that item the reference holds.
nlohmann::json ItemCount(Harness&, const nlohmann::json& acRequest)
{
    const auto cCount = EngineCalls::GetItemCount(acRequest.value("object", 0u), acRequest.value("item", 0u));
    if (!cCount)
        return Error("no such object or item, or it holds no inventory");
    return {{"ok", true}, {"count", *cCount}};
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

// {"cmd": "door_open", "object": id, "open": false, "snap": true}: sets the door's state in this game only
// (staging: puts a door back the way a scenario found it).
nlohmann::json DoorOpen(Harness&, const nlohmann::json& acRequest)
{
    const uint32_t cObject = acRequest.value("object", 0u);
    if (!EngineCalls::SetDoorOpen(cObject, acRequest.value("open", false), acRequest.value("snap", true)))
        return Error("no such door");
    spdlog::info("Door (rpc): {:X} open {}", cObject, acRequest.value("open", false));
    return Ok();
}

// {"cmd": "camera", "target": id}: what the player's camera shows (EngineCalls::GetCameraView); with a target,
// where its head is on screen.
nlohmann::json Camera(Harness&, const nlohmann::json& acRequest)
{
    const auto cView = EngineCalls::GetCameraView(acRequest.value("target", 0u));
    if (!cView)
        return Error("no camera");
    nlohmann::json response = {{"ok", true},
                               {"state", cView->State},
                               {"position", {cView->Position[0], cView->Position[1], cView->Position[2]}},
                               {"yaw", cView->Yaw},
                               {"pitch", cView->Pitch},
                               {"crosshair", cView->Crosshair}};
    if (acRequest.contains("target"))
        response["target"] = {{"on_screen", cView->TargetOnScreen},
                              {"screen", {cView->TargetScreen[0], cView->TargetScreen[1]}},
                              {"off_center", cView->TargetOffCenter}};
    return response;
}

constexpr Command cCommands[] = {
    {"status", false, &GetStatus},
    {"camera", true, &Camera},
    {"graph_bool", true, &GraphBool},
    {"kill_move_check", true, &KillMoveCheckCommand},
    {"anim_trace", true, &AnimTrace},
    {"idle_conditions", true, &IdleConditions},
    {"objects", true, &Objects},
    {"quest", true, &Quest},
    {"form_save", true, &FormSave},
    {"engine_events", false, &GetEngineEvents},
    {"form_load", true, &FormLoad},
    {"script_state", true, &ScriptState},
    {"script_var", true, &ScriptVar},
    {"script_stacks", true, &ScriptStacks},
    {"item_count", true, &ItemCount},
    {"activate", true, &Activate},
    {"lock", true, &Lock},
    {"door_open", true, &DoorOpen},
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
