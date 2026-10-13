#include <TiltedOnlinePCH.h>
#include "Commands.h"
#include "EngineCalls.h"
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
    {"status", false, &GetStatus}, {"graph_bool", true, &GraphBool}, {"objects", true, &Objects},
    {"activate", true, &Activate}, {"lock", true, &Lock}, {"graph_state", true, &GraphState},
    {"load_graph_state", true, &LoadGraphState}, {"graph_writers", true, &GraphWriters},
};
} // namespace

std::span<const Command> WorldCommands()
{
    return cCommands;
}
} // namespace TestHarness
