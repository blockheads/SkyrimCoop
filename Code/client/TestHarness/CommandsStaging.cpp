#include <TiltedOnlinePCH.h>
#include "Commands.h"
#include "ConsoleOutput.h"
#include "EngineCalls.h"

// Setting up a scenario: console lines, menus, the camera, where the player stands, a copy's drift.
namespace TestHarness
{
namespace
{
// {"cmd": "console", "line": "...", "target"?: id}: queues the line as the console does, on the target reference if
// given (as the console's selected one); "ticket" reads its result (console_result).
nlohmann::json Console(Harness&, const nlohmann::json& acRequest)
{
    const std::string cLine = acRequest.value("line", "");
    const uint32_t cTarget = acRequest.value("target", 0u);
    if (cTarget)
        spdlog::info("Console command (rpc): {:X}: {}", cTarget, cLine);
    else
        spdlog::info("Console command (rpc): {}", cLine);
    const uint64_t cTicket = ConsoleOutput::Expect(cLine, cTarget);
    EngineCalls::RunConsoleCommand(cLine);
    return {{"ok", true}, {"ticket", cTicket}};
}

// {"cmd": "console_result", "ticket": n}: whether the line ran yet and, once it has, what the console printed for it.
nlohmann::json ConsoleResult(Harness&, const nlohmann::json& acRequest)
{
    const auto cOutput = ConsoleOutput::Take(acRequest.value("ticket", uint64_t(0)));
    return {{"ok", true}, {"ran", cOutput.has_value()}, {"output", cOutput ? nlohmann::json(*cOutput) : nlohmann::json::array()}};
}

// {"cmd": "menu", "name": "Console", "show": true}: opens or closes a menu like the game.
nlohmann::json Menu(Harness&, const nlohmann::json& acRequest)
{
    const std::string cName = acRequest.value("name", "");
    const bool cShow = acRequest.value("show", true);
    spdlog::info("Menu (rpc): {} {}", cShow ? "show" : "hide", cName);
    EngineCalls::SetMenuShown(cName.c_str(), cShow);
    return Ok();
}

// {"cmd": "first_person"}: the player's camera to first person.
nlohmann::json FirstPerson(Harness&, const nlohmann::json&)
{
    EngineCalls::ForcePlayerFirstPerson();
    return Ok();
}

// {"cmd": "strip", "actor": id}: unequips everything on it in this game only (a copy whose equipment
// drifted from its owner's).
nlohmann::json Strip(Harness&, const nlohmann::json& acRequest)
{
    EngineCalls::UnequipAllUnsynced(acRequest.value("actor", 0u));
    return Ok();
}

// {"cmd": "freeze", "actor": id, "frozen": true}: freezes the actor in this game (EngineCalls::FreezeActor): a
// scenario's target stands still, does not fight back and lets players pass through; false thaws it.
nlohmann::json Freeze(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::FreezeActor(acRequest.value("actor", 0u), acRequest.value("frozen", true)) ? Ok() : Error("no such actor");
}

// {"cmd": "move_actor", "actor": id, "position": [x, y, z]}: brings the actor (loaded or not) to that spot in the
// player's cell; {"cmd": "move_actor", "actor": id, "home": true} sends it back where the game placed it. This game only.
nlohmann::json MoveActor(Harness&, const nlohmann::json& acRequest)
{
    const uint32_t cActor = acRequest.value("actor", 0u);
    if (acRequest.value("home", false))
        return EngineCalls::MoveActorHome(cActor) ? Ok() : Error("no such actor, or no editor location");
    const auto& cPosition = acRequest.at("position");
    const float cSpot[3]{cPosition[0].get<float>(), cPosition[1].get<float>(), cPosition[2].get<float>()};
    return EngineCalls::MoveActorToPlayer(cActor, cSpot) ? Ok() : Error("no such actor");
}

// {"cmd": "face", "target": id}: the player turns toward the actor (yaw and pitch).
nlohmann::json Face(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::FacePlayerToward(acRequest.value("target", 0u)) ? Ok() : Error("no such target");
}

// {"cmd": "place", "target": id, "distance": 120, "bearing": 90}: the player stands that far from the reference (on the
// bearing's side if given), facing it
// (a move, not a teleport).
nlohmann::json Place(Harness&, const nlohmann::json& acRequest)
{
    const auto cBearing = acRequest.contains("bearing") ? std::optional<float>(acRequest["bearing"].get<float>()) : std::nullopt;
    return EngineCalls::PlacePlayerBefore(acRequest.value("target", 0u), acRequest.value("distance", 120.f), cBearing) ? Ok() : Error("no such target");
}

// {"cmd": "clear_map_marker"}: removes the player's map marker in this game only (outside the map, nothing is sent).
nlohmann::json ClearMapMarker(Harness&, const nlohmann::json&)
{
    EngineCalls::ClearPlayerMapMarker();
    return Ok();
}

constexpr Command cCommands[] = {
    {"console", true, &Console}, {"console_result", false, &ConsoleResult}, {"menu", false, &Menu}, {"first_person", true, &FirstPerson},
    {"strip", true, &Strip},     {"face", true, &Face},  {"place", true, &Place},
    {"freeze", true, &Freeze},   {"move_actor", true, &MoveActor}, {"clear_map_marker", true, &ClearMapMarker},
};
} // namespace

std::span<const Command> StagingCommands()
{
    return cCommands;
}
} // namespace TestHarness
