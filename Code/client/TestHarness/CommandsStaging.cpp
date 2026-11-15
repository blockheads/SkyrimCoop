#include "Commands.h"
#include "ConsoleOutput.h"
#include "EngineCalls.h"
#include "SwingWatch.h"

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

nlohmann::json MenuFlags(Harness&, const nlohmann::json& acRequest)
{
    const auto cFlags = EngineCalls::GetMenuFlags(acRequest.value("name", std::string()).c_str());
    const auto cHidden = EngineCalls::IsFirstPersonModelHidden();
    const auto cLocal = EngineCalls::IsLocalMapShown();
    const auto cReady = EngineCalls::IsLocalMapReady();
    return {{"ok", true}, {"flags", cFlags ? nlohmann::json(*cFlags) : nlohmann::json(nullptr)},
            {"pauses", EngineCalls::GetMenuPauseCount()},
            {"first_person_hidden", cHidden ? nlohmann::json(*cHidden) : nlohmann::json(nullptr)},
            {"local_map", cLocal ? nlohmann::json(*cLocal) : nlohmann::json(nullptr)},
            {"local_map_ready", cReady ? nlohmann::json(*cReady) : nlohmann::json(nullptr)}};
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

// {"cmd": "copy_do_nothing", "actor": id}: this copy holds its own do-nothing package, as when its owner's package is one
// the engine made (EngineCalls::CopyHoldsDoNothing).
nlohmann::json CopyDoNothing(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::CopyHoldsDoNothing(acRequest.value("actor", 0u)) ? Ok() : Error("no such copy");
}

// {"cmd": "put_package", "actor": id, "package": id}: the actor runs that package (EngineCalls::PutPackage). This game only.
nlohmann::json PutPackage(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::PutPackage(acRequest.value("actor", 0u), acRequest.value("package", 0u)) ? Ok() : Error("no such actor or package, or no AI process");
}

// {"cmd": "face", "target": id}: the player turns toward an actor's chest or an object (yaw and pitch).
nlohmann::json Face(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::FacePlayerToward(acRequest.value("target", 0u)) ? Ok() : Error("no such target");
}

GameSwingCast::Reach ReachOf(const nlohmann::json& acRequest) noexcept
{
    const std::string cReach = acRequest.value("reach", "weapon");
    return cReach == "two_hand" ? GameSwingCast::Reach::kTwoHand : cReach == "h2h" ? GameSwingCast::Reach::kHandToHand : GameSwingCast::Reach::kWeapon;
}

nlohmann::json SwingCastJson(const GameSwingCast::Cast& acCast)
{
    nlohmann::json hits = nlohmann::json::array();
    for (const auto& cHit : acCast.Hits)
        hits.push_back({{"ref", cHit.Reference}, {"layer", cHit.Layer}, {"point", cHit.Point}});
    return {{"ok", acCast.Cast}, {"eye", acCast.Eye}, {"direction", acCast.Direction}, {"reach", acCast.Reach}, {"radius", acCast.Radius}, {"hits", hits}};
}

// {"cmd": "swing_cast", "reach": "weapon" | "two_hand" | "h2h"}: what the player's melee swing would hit now, nearest
// first (the engine's own cast: GameSwingCast); a swing takes the first.
nlohmann::json SwingCast(Harness&, const nlohmann::json& acRequest)
{
    const auto cCast = EngineCalls::PlayerSwingCast(ReachOf(acRequest));
    return cCast ? SwingCastJson(*cCast) : Error("no player");
}

// {"cmd": "aim_swing", "point": [x, y, z], "reach": ...}: the player's eye looks at the point (first person), then
// swing_cast's answer. The eye follows the turn: aim again until the first hit is the one wanted.
nlohmann::json AimSwing(Harness&, const nlohmann::json& acRequest)
{
    const auto cPoint = acRequest.value("point", std::array<float, 3>{});
    if (!EngineCalls::AimPlayerEyeAt(cPoint.data()))
        return Error("no player");
    const auto cCast = EngineCalls::PlayerSwingCast(ReachOf(acRequest));
    return cCast ? SwingCastJson(*cCast) : Error("no player");
}

// {"cmd": "last_swing"}: the player's last melee swing as the engine cast it (TestHarness::SwingWatch): its count, eye,
// direction, reach and hits, nearest first (the swing took the first).
nlohmann::json LastSwing(Harness&, const nlohmann::json&)
{
    const auto cLast = SwingWatch::Last();
    auto answer = SwingCastJson(cLast.Cast);
    answer["ok"] = true;
    answer["count"] = cLast.Count;
    return answer;
}

// {"cmd": "place", "target": id, "distance": 120, "bearing": 90}: the player stands that far from the reference (on the
// bearing's side if given), facing it (a move, not a teleport). With "position": [x, y, z], it stands there (its
// feet), facing the target if one is given: coop_test.py picks a spot it checked with rays.
nlohmann::json Place(Harness&, const nlohmann::json& acRequest)
{
    if (acRequest.contains("position"))
    {
        const auto cPosition = acRequest["position"].get<std::array<float, 3>>();
        return EngineCalls::PlacePlayerAt(cPosition.data(), acRequest.value("target", 0u)) ? Ok() : Error("no player or no such target");
    }
    const auto cBearing = acRequest.contains("bearing") ? std::optional<float>(acRequest["bearing"].get<float>()) : std::nullopt;
    const auto cPlacement = EngineCalls::PlacePlayerBefore(acRequest.value("target", 0u), acRequest.value("distance", 120.f), cBearing);
    if (!cPlacement.Placed)
        return Error("no clear spot: " + cPlacement.Blocked);
    return {{"ok", true}, {"bearing", cPlacement.Bearing}, {"position", cPlacement.Position}};
}

// {"cmd": "clear_map_marker"}: removes the player's map marker in this game only (outside the map, nothing is sent).
nlohmann::json ClearMapMarker(Harness&, const nlohmann::json&)
{
    EngineCalls::ClearPlayerMapMarker();
    return Ok();
}

constexpr Command cCommands[] = {
    {"console", true, &Console}, {"console_result", false, &ConsoleResult}, {"menu", false, &Menu}, {"menu_flags", false, &MenuFlags}, {"first_person", true, &FirstPerson},
    {"strip", true, &Strip},     {"face", true, &Face},  {"place", true, &Place}, {"swing_cast", true, &SwingCast}, {"aim_swing", true, &AimSwing}, {"last_swing", false, &LastSwing},
    {"freeze", true, &Freeze},   {"move_actor", true, &MoveActor}, {"put_package", true, &PutPackage}, {"copy_do_nothing", true, &CopyDoNothing}, {"clear_map_marker", true, &ClearMapMarker},
};
} // namespace

std::span<const Command> StagingCommands()
{
    return cCommands;
}
} // namespace TestHarness
