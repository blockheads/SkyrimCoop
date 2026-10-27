#include "Commands.h"
#include "EngineCalls.h"
#include "Harness.h"
#include "Status.h"

#include <World.h>
#include <FormIds.h>
#include <Papyrus/GuestScripts.h>
#include <Bridges/IActorBridge.h>
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
// {"cmd": "body_trace", "refs": [id, ...]}: starts recording these references' physics each frame (BodyTrace), or
// adds them; {"cmd": "body_trace"} alone takes what was recorded so far, {"cmd": "body_trace", "stop": true} the rest.
nlohmann::json BodyTraceCommand(Harness& aHarness, const nlohmann::json& acRequest)
{
    if (acRequest.value("stop", false))
        return {{"ok", true}, {"entries", aHarness.Bodies.Stop()}};
    if (acRequest.contains("refs"))
        aHarness.Bodies.Watch(acRequest.value("refs", std::set<uint32_t>{}));
    return {{"ok", true}, {"entries", aHarness.Bodies.Take()}};
}

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

// {"cmd": "log_trail", "since": n}: the plugin's log lines (info and up) numbered n and later (LogTrail).
nlohmann::json GetLogTrail(Harness& aHarness, const nlohmann::json& acRequest)
{
    return aHarness.Logs.Since(acRequest.value("since", uint64_t(0)));
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
    constexpr uint8_t cContainer = 28; // FormType::Container
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
             {"container", forms.IsObjectBaseFormType(cId, cContainer)},
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

// {"cmd": "world_state", "ids"?: [id, ...], "details"?: true}: every reference in the loaded cells (or only those)
// as this game holds and shows it, with details its pose, physics, graph and effects too
// (EngineCalls::GetLoadedReferences); fields at their defaults are left out.
nlohmann::json WorldState(Harness&, const nlohmann::json& acRequest)
{
    const auto cIds = acRequest.value("ids", std::vector<uint32_t>{});
    auto references = nlohmann::json::array();
    for (const auto& cRef : EngineCalls::GetLoadedReferences(acRequest.value("details", false)))
    {
        if (!cIds.empty() && std::ranges::find(cIds, cRef.Id) == cIds.end())
            continue;
        nlohmann::json ref = {{"id", cRef.Id},
                              {"base", cRef.Base},
                              {"type", cRef.BaseType},
                              {"cell", cRef.Cell},
                              {"flags", cRef.Flags},
                              {"position", {cRef.Position[0], cRef.Position[1], cRef.Position[2]}},
                              {"rotation", {cRef.Rotation[0], cRef.Rotation[1], cRef.Rotation[2]}},
                              {"scale", cRef.Scale},
                              {"has_3d", cRef.Has3D}};
        if (cRef.Actor)
            ref["actor"] = true;
        if (cRef.Has3D)
        {
            ref["hidden"] = cRef.Hidden;
            ref["position_3d"] = {cRef.Position3D[0], cRef.Position3D[1], cRef.Position3D[2]};
            ref["branch"] = {{"name", cRef.Branch}, {"index", cRef.BranchIndex}};
            ref["flags_3d"] = cRef.Flags3D;
        }
        if (cRef.HasLock)
            ref["lock"] = {{"locked", cRef.Locked}, {"level", cRef.LockLevel}};
        if (cRef.OpenState)
            ref["open_state"] = cRef.OpenState;
        if (cRef.Owner)
            ref["owner"] = cRef.Owner;
        if (cRef.Changes)
            ref["changes"] = cRef.Changes;
        if (cRef.Leveled)
            ref["leveled"] = cRef.Leveled;
        if (cRef.Stored)
            ref["stored"] = true;
        if (cRef.Items)
        {
            auto items = nlohmann::json::object();
            for (const auto& [cItem, cCount] : *cRef.Items)
                if (cCount)
                    items[std::format("{:X}", cItem)] = cCount;
            ref["items"] = items;
        }
        if (!cRef.Pose.empty())
        {
            auto pose = nlohmann::json::object();
            for (const auto& cNode : cRef.Pose)
            {
                // A name met again gets its number: "Door", "Door#2".
                std::string key = cNode.Name;
                for (int n = 2; pose.contains(key); ++n)
                    key = std::format("{}#{}", cNode.Name, n);
                pose[key] = {{"position", cNode.Position}, {"rotate", cNode.Rotate}};
            }
            ref["pose"] = pose;
        }
        if (!cRef.Collision.empty())
        {
            auto bodies = nlohmann::json::array();
            for (const auto& cBody : cRef.Collision)
                bodies.push_back({{"node", cBody.Node}, {"in_world", cBody.InWorld}, {"position", cBody.Position}, {"motion", cBody.Motion},
                                  {"awake", cBody.Awake}, {"rotation", cBody.Rotation}, {"velocity", cBody.Velocity},
                                  {"flags", cBody.Flags}, {"offset", cBody.Offset}});
            ref["collision"] = bodies;
        }
        if (!cRef.Graph.empty())
        {
            auto graph = nlohmann::json::object();
            for (const auto& [cName, cValue] : cRef.Graph)
                graph[cName] = cValue;
            ref["graph"] = graph;
        }
        if (!cRef.Effects.empty())
            ref["effects"] = cRef.Effects;
        references.push_back(std::move(ref));
    }
    return {{"ok", true}, {"references", references}};
}

// {"cmd": "quest", "quest": id}: the quest's live state (EngineCalls::GetQuestState), to check what a save holds.
nlohmann::json Quest(Harness&, const nlohmann::json& acRequest)
{
    const auto cState = EngineCalls::GetQuestState(acRequest.value("quest", 0u));
    if (!cState)
        return Error("no such quest");
    auto aliases = nlohmann::json::array();
    for (const auto& cAlias : cState->Aliases)
        aliases.push_back({{"id", cAlias.Id}, {"name", cAlias.Name}, {"ref", cAlias.Ref}, {"base", cAlias.Base}, {"location", cAlias.Location}});
    return {{"ok", true},
            {"flags", cState->Flags},
            {"current_stage", cState->CurrentStage},
            {"already_run", cState->AlreadyRun},
            {"stages", cState->Stages},
            {"objectives", cState->Objectives},
            {"aliases", aliases}};
}

// {"cmd": "quest_allow_rerun", "quest": id}: a run-once quest a scenario started can start again (alreadyRun cleared).
nlohmann::json QuestAllowRerun(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::AllowQuestRerun(acRequest.value("quest", 0u)))
        return Error("no such quest");
    return {{"ok", true}};
}

// {"cmd": "quest_force_alias", "quest", "alias", "ref", "parent", "x", "y"}: a scenario's fill of a reference alias, the
// reference read from its plugin as a quest's own fill reads one (a reference in a cell no game loaded).
nlohmann::json QuestForceAlias(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::ForceQuestAlias(acRequest.value("quest", 0u), acRequest.value("alias", 0u), acRequest.value("ref", 0u),
                                      acRequest.value("parent", 0u), static_cast<int16_t>(acRequest.value("x", 0)),
                                      static_cast<int16_t>(acRequest.value("y", 0))))
        return Error("no such quest alias, or the reference could not be loaded");
    return {{"ok", true}};
}

// {"cmd": "global", "global": id}: a global variable's value.
nlohmann::json Global(Harness&, const nlohmann::json& acRequest)
{
    const auto cValue = EngineCalls::GetGlobalValue(acRequest.value("global", 0u));
    if (!cValue)
        return Error("no such global");
    return {{"ok", true}, {"value", *cValue}};
}

// {"cmd": "script_state", "object": id, "script": name, "state"?: name}: the state that script on the object is
// in, put in that state first when one is given.
nlohmann::json ScriptState(Harness&, const nlohmann::json& acRequest)
{
    if (acRequest.contains("state") && !EngineCalls::SetScriptState(acRequest.value("object", 0u), acRequest.value("script", std::string()).c_str(),
                                                                    acRequest.value("state", std::string()).c_str()))
        return Error("no such script on that object");
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

// {"cmd": "script_var", "object": id, "script": name, "var": name, "value"?: number, "form"?: id}: a script
// variable or auto property (its "::<Name>_var"), set first when a value is given (a number, or a form for an object).
nlohmann::json ScriptVar(Harness&, const nlohmann::json& acRequest)
{
    const auto cObject = acRequest.value("object", 0u);
    const auto cScript = acRequest.value("script", std::string());
    auto name = acRequest.value("var", std::string());
    if (!EngineCalls::GetScriptVariable(cObject, cScript.c_str(), name.c_str()))
        name = "::" + name + "_var";
    if (acRequest.contains("value") && !EngineCalls::SetScriptVariable(cObject, cScript.c_str(), name.c_str(), acRequest["value"].get<double>()))
        return Error("no such int, float or bool variable");
    if (acRequest.contains("form") && !EngineCalls::SetScriptVariableForm(cObject, cScript.c_str(), name.c_str(), acRequest.value("form", 0u)))
        return Error("no such object variable, or no such form");
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

// {"cmd": "global_natives", "class": name}: the class's global natives, each with whether it acts on this game's
// player (NativeReach): {name: bool}.
nlohmann::json GlobalNatives(Harness&, const nlohmann::json& acRequest)
{
    const auto cNatives = EngineCalls::GetGlobalNatives(acRequest.value("class", std::string()).c_str());
    if (!cNatives)
        return Error("no such script class in the VM");
    auto natives = nlohmann::json::object();
    for (const auto& acNative : *cNatives)
        natives[acNative.Name] = acNative.ReachesThisPlayer;
    return {{"ok", true}, {"natives", natives}};
}

// {"cmd": "form", "form": id}: the form's type (TESForm::formType), name, owner (a reference's) and whether the player
// knows it (a word of power).
nlohmann::json Form(Harness&, const nlohmann::json& acRequest)
{
    auto& forms = World::Get().GetFormBridge();
    const uint32_t cForm = acRequest.value("form", 0u);
    if (!forms.FormExists(cForm))
        return Error("no such form");
    return {{"ok", true}, {"type", forms.GetFormType(cForm)}, {"name", forms.GetFormName(cForm)}, {"owner", EngineCalls::GetOwner(cForm)},
            {"known", EngineCalls::GetPlayerKnows(cForm).value_or(false)}};
}

// {"cmd": "form_known", "form": id, "known": bool}: the player knows the form (a word of power) or not.
nlohmann::json FormKnown(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::SetPlayerKnows(acRequest.value("form", 0u), acRequest.value("known", false)))
        return Error("no such form");
    return {{"ok", true}};
}

// {"cmd": "item_count", "object": id, "item": id}: how many of that item the reference holds.
nlohmann::json ItemCount(Harness&, const nlohmann::json& acRequest)
{
    // "form" alone: the player's count of it.
    const auto cCount = EngineCalls::GetItemCount(acRequest.value("object", 0x14u), acRequest.value("item", acRequest.value("form", 0u)));
    if (!cCount)
        return Error("no such object or item, or it holds no inventory");
    return {{"ok", true}, {"count", *cCount}};
}

// {"cmd": "container_items", "object": id}: every item its container lists or holds, with its count.
nlohmann::json ContainerItems(Harness&, const nlohmann::json& acRequest)
{
    const auto cItems = EngineCalls::GetContainerItems(acRequest.value("object", 0u));
    if (!cItems)
        return Error("no such object, or it has no container");
    auto items = nlohmann::json::array();
    for (const auto& [cItem, cCount] : *cItems)
        items.push_back({{"item", cItem}, {"count", cCount}});
    return {{"ok", true}, {"items", items}};
}

// An item with its extra data as the commands read and write it: base and enchantment (and poison) form ids, health
// (tempering, 1 none), enchantment charge, its name.
nlohmann::json StackJson(const Inventory::Entry& acItem)
{
    return {{"item", ToGame(acItem.BaseId)},
            {"count", acItem.Count},
            {"health", acItem.ExtraHealth},
            {"enchantment", acItem.ExtraEnchantId.ModId == 0xFFFFFFFF ? 0xFF000000 | acItem.ExtraEnchantId.BaseId : ToGame(acItem.ExtraEnchantId)},
            {"enchant_charge", acItem.ExtraEnchantCharge},
            {"charge", acItem.ExtraCharge},
            {"poison", ToGame(acItem.ExtraPoisonId)},
            {"poison_count", acItem.ExtraPoisonCount},
            {"soul", acItem.ExtraSoulLevel},
            {"name", acItem.ExtraName}};
}

// {"cmd": "item_stacks", "object": id}: a loose item's own count and extra data, or each stack with extra data a
// container or actor holds (StackJson's fields).
nlohmann::json ItemStacks(Harness&, const nlohmann::json& acRequest)
{
    const auto cStacks = EngineCalls::GetItemStacks(acRequest.value("object", 0x14u));
    if (!cStacks)
        return Error("no such object");
    auto stacks = nlohmann::json::array();
    for (const auto& cStack : *cStacks)
        stacks.push_back(StackJson(cStack));
    return {{"ok", true}, {"stacks", stacks}};
}

// {"cmd": "give_item", "item": id, "count": 1, "health": 1.4, "enchantment": id, "enchant_charge": 1000, "charge": 800,
// "name": "Old Friend"}: the player gets it with that extra data (as an enchanting table's or a forge's).
nlohmann::json GiveItem(Harness&, const nlohmann::json& acRequest)
{
    Inventory::Entry item;
    item.BaseId = ToSession(acRequest.value("item", 0u)).value_or(GameId{});
    item.Count = acRequest.value("count", 1);
    item.ExtraHealth = acRequest.value("health", 0.f);
    item.ExtraEnchantId = ToSession(acRequest.value("enchantment", 0u)).value_or(GameId{});
    item.ExtraEnchantCharge = static_cast<uint16_t>(acRequest.value("enchant_charge", 0u));
    item.ExtraCharge = acRequest.value("charge", 0.f);
    item.ExtraName = acRequest.value("name", std::string());
    if (!EngineCalls::GiveItem(item))
        return Error("no such item");
    return Ok();
}

// {"cmd": "drop", "item": id, "count": 1, "plain": false}: the player drops it as the inventory's Drop does, its stack
// with extra data (or a plain one); the loose item it made.
nlohmann::json Drop(Harness&, const nlohmann::json& acRequest)
{
    const auto cDropped = EngineCalls::PlayerDrops(acRequest.value("item", 0u), acRequest.value("count", 1), acRequest.value("plain", false));
    if (!cDropped)
        return Error("the player holds no such stack, or the drop made no loose item");
    return {{"ok", true}, {"object", *cDropped}};
}

// {"cmd": "store", "item": id, "count": 1, "container": id, "plain": false}: the player stores it in the container as the
// container menu's Store does, its stack with extra data (or a plain one); no container: removes that stack.
nlohmann::json Store(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::PlayerStores(acRequest.value("item", 0u), acRequest.value("count", 1), acRequest.value("container", 0u),
                                   acRequest.value("plain", false)))
        return Error("the player holds no such stack, or no such container");
    return Ok();
}

// {"cmd": "ragdoll", "actor": id}: the actor's ragdoll bodies in order: motion type (4 keyframed), awake, position,
// rotation (x, y, z, w) and velocity in game units.
nlohmann::json Ragdoll(Harness&, const nlohmann::json& acRequest)
{
    const auto cBodies = EngineCalls::GetRagdoll(acRequest.value("actor", 0u));
    if (!cBodies)
        return Error("no such actor");
    auto bodies = nlohmann::json::array();
    for (const auto& cBody : *cBodies)
        bodies.push_back({{"in_world", cBody.InWorld}, {"motion", cBody.Motion}, {"awake", cBody.Awake}, {"position", cBody.Position},
                          {"rotation", cBody.Rotation}, {"velocity", cBody.Velocity},
                          {"bone", cBody.HasBone ? nlohmann::json(cBody.Bone) : nlohmann::json()}});
    return {{"ok", true}, {"bodies", bodies}};
}

nlohmann::json RayJson(const EngineCalls::RayHit& acHit)
{
    if (!acHit.Hit)
        return {{"hit", false}, {"distance", acHit.Distance}};
    return {{"hit", true}, {"point", acHit.Point}, {"normal", acHit.Normal}, {"distance", acHit.Distance},
            {"ref", acHit.Reference}, {"layer", acHit.Layer}};
}

// {"cmd": "ray", "from": [x, y, z], "to": [x, y, z], "layer": 30}: the first collision on that line as the layer sees it
// (30, the character controller's: what stops a player walking): point, normal, distance, its reference and layer.
nlohmann::json Ray(Harness&, const nlohmann::json& acRequest)
{
    if (!acRequest.contains("from") || !acRequest.contains("to"))
        return Error("from and to needed");
    const auto cFrom = acRequest["from"].get<std::array<float, 3>>();
    const auto cTo = acRequest["to"].get<std::array<float, 3>>();
    const auto cHit = EngineCalls::CastRay(cFrom.data(), cTo.data(), acRequest.value("layer", 30u));
    if (!cHit)
        return Error("no physics world, or this frame's picks are spent");
    return {{"ok", true}, {"ray", RayJson(*cHit)}};
}

// {"cmd": "surroundings", "at": [x, y, z], "radius": 500}: what is around a spot (the player's feet by default), as a
// walking player meets it: the floor below and the ceiling above, and rings of rays at knee (30) and chest (100)
// height in 16 directions (bearing 0 north, 90 east), each the first collision within the radius.
nlohmann::json Surroundings(Harness&, const nlohmann::json& acRequest)
{
    std::array<float, 3> at{};
    if (acRequest.contains("at"))
        at = acRequest["at"].get<std::array<float, 3>>();
    else
        World::Get().GetActorBridge().GetActorPosition(0x14, at[0], at[1], at[2]);
    const float cRadius = acRequest.value("radius", 500.f);
    const auto cCast = [](std::array<float, 3> aFrom, std::array<float, 3> aTo) { return EngineCalls::CastRay(aFrom.data(), aTo.data()); };
    const auto cFloor = cCast({at[0], at[1], at[2] + 60.f}, {at[0], at[1], at[2] - 1000.f});
    const auto cCeiling = cCast({at[0], at[1], at[2] + 60.f}, {at[0], at[1], at[2] + 2000.f});
    if (!cFloor || !cCeiling)
        return Error("no physics world, or this frame's picks are spent");
    auto rings = nlohmann::json::object();
    for (const float cHeight : {30.f, 100.f})
    {
        auto ring = nlohmann::json::array();
        for (int bearing = 0; bearing < 360; bearing += 360 / 16)
        {
            const float cRadians = glm::radians(static_cast<float>(bearing));
            const std::array<float, 3> cFrom{at[0], at[1], at[2] + cHeight};
            const std::array<float, 3> cTo{at[0] + std::sin(cRadians) * cRadius, at[1] + std::cos(cRadians) * cRadius, at[2] + cHeight};
            const auto cHit = cCast(cFrom, cTo);
            if (!cHit)
                return Error("this frame's picks are spent");
            auto ray = RayJson(*cHit);
            ray["bearing"] = bearing;
            ring.push_back(ray);
        }
        rings[cHeight < 50.f ? "knee" : "chest"] = ring;
    }
    return {{"ok", true}, {"at", at}, {"floor", RayJson(*cFloor)}, {"ceiling", RayJson(*cCeiling)}, {"rings", rings}};
}

// {"cmd": "menu_var", "menu": "ContainerMenu", "path": "_root.Menu_mc.bFadedIn"}: a number, boolean or string in an
// open menu's movie.
nlohmann::json MenuVar(Harness&, const nlohmann::json& acRequest)
{
    const auto cMenu = acRequest.value("menu", std::string());
    const auto cPath = acRequest.value("path", std::string());
    if (const auto cValue = EngineCalls::GetMenuVariable(cMenu.c_str(), cPath.c_str()))
        return {{"ok", true}, {"value", *cValue}};
    if (const auto cText = EngineCalls::GetMenuText(cMenu.c_str(), cPath.c_str()))
        return {{"ok", true}, {"value", *cText}};
    return Error("no such menu open, or no number, boolean or string there");
}

// {"cmd": "ref_enabled", "object": id}: whether the reference is enabled in this game.
nlohmann::json RefEnabled(Harness&, const nlohmann::json& acRequest)
{
    const auto cEnabled = EngineCalls::IsReferenceEnabled(acRequest.value("object", 0u));
    if (!cEnabled)
        return Error("no such reference");
    return {{"ok", true}, {"enabled", *cEnabled}};
}

// {"cmd": "actor_place", "actor": id}: where this game has the actor, loaded or not, and whether it is a copy here.
nlohmann::json ActorPlace(Harness&, const nlohmann::json& acRequest)
{
    auto& actors = World::Get().GetActorBridge();
    const uint32_t cActor = acRequest.value("actor", 0u);
    if (!actors.ActorExists(cActor))
        return Error("no such actor");
    float position[3]{};
    actors.GetActorPosition(cActor, position[0], position[1], position[2]);
    return {{"ok", true},
            {"cell", actors.GetActorParentCellFormId(cActor)},
            {"worldspace", actors.GetActorWorldSpaceFormId(cActor)},
            {"position", {position[0], position[1], position[2]}},
            {"has_3d", actors.ActorHas3D(cActor)},
            {"disabled", actors.IsActorDisabled(cActor)},
            {"deleted", actors.IsActorDeleted(cActor)},
            {"copy", actors.IsActorCopy(cActor)}};
}

// {"cmd": "is_current_package", "actor": id, "package": id}: the condition GetIsCurrentPackage on the actor here.
nlohmann::json IsCurrentPackage(Harness&, const nlohmann::json& acRequest)
{
    const auto cResult = EngineCalls::IsCurrentPackage(acRequest.value("actor", 0u), acRequest.value("package", 0u));
    if (!cResult)
        return Error("no such actor or package");
    return {{"ok", true}, {"result", *cResult}};
}

// {"cmd": "faction_rank", "actor": id, "faction": id}: the condition GetFactionRank on the actor here (-1: none).
nlohmann::json FactionRank(Harness&, const nlohmann::json& acRequest)
{
    const auto cResult = EngineCalls::GetFactionRank(acRequest.value("actor", 0u), acRequest.value("faction", 0u));
    if (!cResult)
        return Error("no such actor or faction");
    return {{"ok", true}, {"result", *cResult}};
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

// {"cmd": "skill", "index": actor value}: the player's skill level and its experience toward the next one. With
// "actor": that actor's value (a copy's, as this game's scripts read it).
nlohmann::json Skill(Harness&, const nlohmann::json& acRequest)
{
    const uint32_t cIndex = acRequest.value("index", 0u);
    if (acRequest.contains("actor"))
        return {{"ok", true}, {"level", EngineCalls::GetActorValue(acRequest.value("actor", 0u), cIndex)}};
    return {{"ok", true}, {"level", EngineCalls::GetPlayerActorValue(cIndex)}, {"experience", EngineCalls::GetPlayerSkillExperience(cIndex)}};
}

// {"cmd": "trainings", "set": n}: how many times the player trained this level; with set, that count first.
nlohmann::json Trainings(Harness&, const nlohmann::json& acRequest)
{
    if (acRequest.contains("set"))
        EngineCalls::SetPlayerTrainings(acRequest.value("set", 0u));
    return {{"ok", true}, {"count", EngineCalls::GetPlayerTrainings()}};
}

// {"cmd": "camera", "target": id}: what the player's camera shows (EngineCalls::GetCameraView): where it is and looks,
// what its view ray meets, whom it follows and whether a ray from their head reaches it; with a target, where its head
// is on screen.
nlohmann::json Camera(Harness&, const nlohmann::json& acRequest)
{
    const auto cView = EngineCalls::GetCameraView(acRequest.value("target", 0u));
    if (!cView)
        return Error("no camera");
    nlohmann::json response = {{"ok", true},
                               {"state", cView->State},
                               {"position", {cView->Position[0], cView->Position[1], cView->Position[2]}},
                               {"forward", {cView->Forward[0], cView->Forward[1], cView->Forward[2]}},
                               {"yaw", cView->Yaw},
                               {"pitch", cView->Pitch},
                               {"crosshair", cView->Crosshair},
                               {"following", cView->Following},
                               {"looking", cView->Looking ? RayJson(*cView->Looking) : nlohmann::json()}};
    if (cView->Following)
    {
        response["followed_head"] = {cView->FollowedHead[0], cView->FollowedHead[1], cView->FollowedHead[2]};
        response["sight"] = cView->Sight ? RayJson(*cView->Sight) : nlohmann::json();
    }
    if (acRequest.contains("target"))
        response["target"] = {{"on_screen", cView->TargetOnScreen},
                              {"screen", {cView->TargetScreen[0], cView->TargetScreen[1]}},
                              {"off_center", cView->TargetOffCenter}};
    return response;
}

// {"cmd": "screen"}: what a cutscene does to this game's screen (EngineCalls::GetScreenState).
nlohmann::json Screen(Harness&, const nlohmann::json&)
{
    const auto cState = EngineCalls::GetScreenState();
    return {{"ok", true}, {"controls", cState.Controls}, {"imods", cState.Imods}, {"shake", {{"strength", cState.ShakeStrength}, {"seconds", cState.ShakeSeconds}}}};
}

// {"cmd": "papyrus_call", "class": name, "function": name, "self"?: form id, "args": [...], "player_action"?: bool}:
// queues the call as a script's: a quest's with no player's action behind it, or (player_action) one this game's
// player's action started (GuestScripts' cause 0x14). Arguments: null (None), bools, integers, floats, strings,
// {"form": id}.
nlohmann::json PapyrusCall(Harness&, const nlohmann::json& acRequest)
{
    std::vector<LocalValue> arguments;
    for (const auto& cArgument : acRequest.value("args", nlohmann::json::array()))
    {
        if (cArgument.is_null())
            arguments.emplace_back(std::monostate{});
        else if (cArgument.is_boolean())
            arguments.emplace_back(cArgument.get<bool>());
        else if (cArgument.is_number_integer())
            arguments.emplace_back(cArgument.get<int32_t>());
        else if (cArgument.is_number())
            arguments.emplace_back(cArgument.get<float>());
        else if (cArgument.is_string())
            arguments.emplace_back(cArgument.get<std::string>());
        else if (cArgument.is_object() && cArgument.contains("form"))
            arguments.emplace_back(LocalForm{cArgument.value("form", 0u)});
        else
            return Error("an argument is not null, a bool, number, string or {\"form\": id}");
    }
    const std::optional<uint32_t> cSelf = acRequest.contains("self") ? std::optional(acRequest.value("self", 0u)) : std::nullopt;
    std::optional<GuestScripts::ScopedCause> cause;
    if (acRequest.value("player_action", false))
        cause.emplace(0x14u);
    if (!EngineCalls::QueuePapyrusCall(acRequest.value("class", std::string()), acRequest.value("function", std::string()), cSelf, arguments))
        return Error("the VM did not queue the call");
    return Ok();
}

constexpr Command cCommands[] = {
    {"status", false, &GetStatus},
    {"camera", true, &Camera},
    {"screen", true, &Screen},
    {"papyrus_call", true, &PapyrusCall},
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
    {"body_trace", true, &BodyTraceCommand},
    {"idle_conditions", true, &IdleConditions},
    {"objects", true, &Objects},
    {"world_state", true, &WorldState},
    {"quest", true, &Quest},
    {"quest_force_alias", true, &QuestForceAlias},
    {"quest_allow_rerun", true, &QuestAllowRerun},
    {"global", true, &Global},
    {"form_save", true, &FormSave},
    {"engine_events", false, &GetEngineEvents},
    {"log_trail", false, &GetLogTrail},
    {"form_load", true, &FormLoad},
    {"script_state", true, &ScriptState},
    {"script_var", true, &ScriptVar},
    {"script_stacks", true, &ScriptStacks},
    {"global_natives", true, &GlobalNatives},
    {"form", true, &Form},
    {"form_known", true, &FormKnown},
    {"container_items", true, &ContainerItems},
    {"ragdoll", true, &Ragdoll},
    {"ray", true, &Ray},
    {"surroundings", true, &Surroundings},
    {"menu_var", false, &MenuVar},
    {"ref_enabled", true, &RefEnabled},
    {"actor_place", true, &ActorPlace},
    {"is_current_package", true, &IsCurrentPackage},
    {"faction_rank", true, &FactionRank},
    {"activate", true, &Activate},
    {"item_stacks", true, &ItemStacks},
    {"give_item", true, &GiveItem},
    {"drop", true, &Drop},
    {"store", true, &Store},
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
