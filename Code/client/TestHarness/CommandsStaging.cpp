#include <TiltedOnlinePCH.h>
#include "Commands.h"
#include "EngineCalls.h"

// Setting up a scenario: console lines, menus, the camera, where the player stands, a copy's drift.
namespace TestHarness
{
namespace
{
// {"cmd": "console", "line": "..."}: runs the line as the console does.
nlohmann::json Console(Harness&, const nlohmann::json& acRequest)
{
    const std::string cLine = acRequest.value("line", "");
    spdlog::info("Console command (rpc): {}", cLine);
    EngineCalls::RunConsoleCommand(cLine);
    return Ok();
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

// {"cmd": "face", "target": id}: the player turns toward the actor (yaw and pitch).
nlohmann::json Face(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::FacePlayerToward(acRequest.value("target", 0u)) ? Ok() : Error("no such target");
}

// {"cmd": "place", "target": id, "distance": 120}: the player stands that far from the actor, facing it
// (a move, not a teleport).
nlohmann::json Place(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::PlacePlayerBefore(acRequest.value("target", 0u), acRequest.value("distance", 120.f)) ? Ok() : Error("no such target");
}

constexpr Command cCommands[] = {
    {"console", true, &Console}, {"menu", false, &Menu}, {"first_person", true, &FirstPerson},
    {"strip", true, &Strip},     {"face", true, &Face},  {"place", true, &Place},
};
} // namespace

std::span<const Command> StagingCommands()
{
    return cCommands;
}
} // namespace TestHarness
