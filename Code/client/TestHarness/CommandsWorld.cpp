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

// {"cmd": "dialogue"}: the player's conversation: whether the menu is open, the speaker, the options in order.
nlohmann::json Dialogue(Harness&, const nlohmann::json&)
{
    const EngineCalls::DialogueState cState = EngineCalls::GetDialogue();
    nlohmann::json options = nlohmann::json::array();
    for (const auto& cOption : cState.Options)
        options.push_back({{"text", cOption.Text}, {"topic", cOption.Topic}, {"info", cOption.Info}, {"quest", cOption.Quest}});
    return {{"ok", true},
            {"menu", EngineCalls::IsMenuOpen("Dialogue Menu")},
            {"open", cState.MenuOpen},
            {"speaker", cState.Speaker},
            {"skip_head", cState.SkipHead},
            {"current_info", cState.CurrentInfo},
            {"last_info", cState.LastInfo},
            {"options", options},
            {"swf", {{"allow_progress", cState.AllowProgress}, {"state", cState.SwfState}, {"selected", cState.Selected},
                     {"cursor", cState.CursorTopic}}}};
}

// {"cmd": "force_greet", "actor": id}: the actor starts a conversation with the player (its ForceGreet package).
nlohmann::json ForceGreet(Harness&, const nlohmann::json& acRequest)
{
    const uint32_t cActor = acRequest.value("actor", 0u);
    spdlog::info("Force greet (rpc): {:X}", cActor);
    return {{"ok", true}, {"started", EngineCalls::ForceGreet(cActor)}};
}

// {"cmd": "info_said", "info": id, "said": false}: whether the line is marked said; with said, set first.
nlohmann::json InfoSaid(Harness&, const nlohmann::json& acRequest)
{
    std::optional<bool> said;
    if (acRequest.contains("said"))
        said = acRequest.value("said", false);
    const auto cSaid = EngineCalls::SetInfoSaid(acRequest.value("info", 0u), said);
    if (!cSaid)
        return Error("no such line");
    return {{"ok", true}, {"said", *cSaid}};
}

// {"cmd": "dialogue_pick", "index": n}: says the conversation's option n, as clicking it does (no menu needed).
nlohmann::json DialoguePick(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::PickDialogue(acRequest.value("index", 0u)))
        return Error("no conversation");
    return Ok();
}

// {"cmd": "item_count", "form": id}: how many of the item the player carries.
nlohmann::json ItemCount(Harness&, const nlohmann::json& acRequest)
{
    const auto cCount = EngineCalls::GetPlayerItemCount(acRequest.value("form", 0u));
    if (!cCount)
        return Error("no such form");
    return {{"ok", true}, {"count", *cCount}};
}

// {"cmd": "skill", "index": actor value}: the player's skill level and its experience toward the next one.
nlohmann::json Skill(Harness&, const nlohmann::json& acRequest)
{
    const uint32_t cIndex = acRequest.value("index", 0u);
    return {{"ok", true}, {"level", EngineCalls::GetPlayerActorValue(cIndex)}, {"experience", EngineCalls::GetPlayerSkillExperience(cIndex)}};
}

// {"cmd": "trainings", "set": n}: how many times the player trained this level; with set, that count first.
nlohmann::json Trainings(Harness&, const nlohmann::json& acRequest)
{
    if (acRequest.contains("set"))
        EngineCalls::SetPlayerTrainings(acRequest.value("set", 0u));
    return {{"ok", true}, {"count", EngineCalls::GetPlayerTrainings()}};
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
    {"dialogue", true, &Dialogue},
    {"dialogue_pick", true, &DialoguePick},
    {"item_count", true, &ItemCount},
    {"skill", true, &Skill},
    {"force_greet", true, &ForceGreet},
    {"info_said", true, &InfoSaid},
    {"trainings", true, &Trainings},
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
