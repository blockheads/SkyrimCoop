#include "Commands.h"
#include "ConsoleOutput.h"
#include "EngineCalls.h"
#include "Harness.h"
#include "RagdollWatch.h"
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
    const auto cLockpick = EngineCalls::IsLockpickBuilt();
    return {{"ok", true}, {"flags", cFlags ? nlohmann::json(*cFlags) : nlohmann::json(nullptr)},
            {"pauses", EngineCalls::GetMenuPauseCount()},
            {"first_person_hidden", cHidden ? nlohmann::json(*cHidden) : nlohmann::json(nullptr)},
            {"local_map", cLocal ? nlohmann::json(*cLocal) : nlohmann::json(nullptr)},
            {"local_map_ready", cReady ? nlohmann::json(*cReady) : nlohmann::json(nullptr)},
            {"lockpick_built", cLockpick ? nlohmann::json(*cLockpick) : nlohmann::json(nullptr)}};
}

// {"cmd": "first_person"}: the player's camera to first person.
nlohmann::json FirstPerson(Harness&, const nlohmann::json&)
{
    EngineCalls::ForcePlayerFirstPerson();
    return Ok();
}

// {"cmd": "third_person"}: the player's camera to third person (its own body drawn, as a copy is).
nlohmann::json ThirdPerson(Harness&, const nlohmann::json&)
{
    EngineCalls::ForcePlayerThirdPerson();
    return Ok();
}

// {"cmd": "strip", "actor": id}: unequips everything on it in this game only (a copy whose equipment
// drifted from its owner's).
nlohmann::json Strip(Harness&, const nlohmann::json& acRequest)
{
    EngineCalls::UnequipAllUnsynced(acRequest.value("actor", 0u));
    return Ok();
}

// {"cmd": "reset_worn", "actor": id}: the engine resets what it wears to its base's outfit (EngineCalls::ResetWornToOutfit):
// a copy's drift as Skyrim makes it; worn_after: what it wears as the reset returns.
nlohmann::json ResetWorn(Harness&, const nlohmann::json& acRequest)
{
    const auto cWorn = EngineCalls::ResetWornToOutfit(acRequest.value("actor", 0u));
    if (!cWorn)
        return Error("no such actor or NPC base");
    return {{"ok", true}, {"worn_after", *cWorn}};
}

// {"cmd": "reset_3d", "actor": id}: DoReset3D(false) in this game only (EngineCalls::Reset3D).
nlohmann::json Reset3DCommand(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::Reset3D(acRequest.value("actor", 0u)) ? Ok() : Error("no such actor");
}

// {"cmd": "freeze", "actor": id, "frozen": true}: freezes the actor in this game (EngineCalls::FreezeActor): a
// scenario's target stands still, does not fight back and lets players pass through; false thaws it.
nlohmann::json Freeze(Harness& aHarness, const nlohmann::json& acRequest)
{
    const uint32_t cActor = acRequest.value("actor", 0u);
    const bool cFrozen = acRequest.value("frozen", true);
    if (!EngineCalls::FreezeActor(cActor, cFrozen))
        return Error("no such actor");
    aHarness.Frozen.Set(cActor, cFrozen);
    return Ok();
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

// {"cmd": "put_on_mount", "rider": id, "mount": id}: the rider on the mount at once (EngineCalls::PutOnMount). This game only.
nlohmann::json PutOnMount(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::PutOnMount(acRequest.value("rider", 0u), acRequest.value("mount", 0u)) ? Ok() : Error("no such rider or mount");
}

// {"cmd": "board_ridden", "rider": id, "mount": id}: the engine's get-on onto a ridden horse (EngineCalls::BoardRiddenMount).
nlohmann::json BoardRidden(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::BoardRiddenMount(acRequest.value("rider", 0u), acRequest.value("mount", 0u)) ? Ok() : Error("refused, or no such rider or mount");
}

// {"cmd": "activate_text", "ref": id}: the prompt the crosshair shows for it (EngineCalls::GetActivateText).
nlohmann::json ActivateTextCommand(Harness&, const nlohmann::json& acRequest)
{
    return {{"ok", true}, {"text", EngineCalls::GetActivateText(acRequest.value("ref", 0u))}};
}

// {"cmd": "graph_event", "actor": id, "event": "PairEnd"}: the event to its graphs (EngineCalls::SendGraphEvent).
nlohmann::json GraphEventCommand(Harness&, const nlohmann::json& acRequest)
{
    return {{"ok", true}, {"taken", EngineCalls::SendGraphEvent(acRequest.value("actor", 0u), acRequest.value("event", std::string{}))}};
}

// {"cmd": "take_reins", "rider": id, "mount": id}: the horse names this rider (EngineCalls::TakeReins). This game only.
nlohmann::json TakeReinsCommand(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::TakeReins(acRequest.value("rider", 0u), acRequest.value("mount", 0u)) ? Ok() : Error("no rider interaction or mount");
}

// {"cmd": "seat_on_mount", "rider": id, "mount": id}: linked and seated at once (EngineCalls::SeatOnMount).
nlohmann::json SeatOnMountCommand(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::SeatOnMount(acRequest.value("rider", 0u), acRequest.value("mount", 0u)) ? Ok() : Error("no such rider or mount");
}

// {"cmd": "sit_on_mount_quick", "rider": id, "mount": id}: the engine's quick get-on (EngineCalls::SitOnMountQuick).
nlohmann::json SitOnMountQuickCommand(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::SitOnMountQuick(acRequest.value("rider", 0u), acRequest.value("mount", 0u)) ? Ok() : Error("refused, or no such rider or mount");
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

// {"cmd": "ragdoll_adds", "clear": false}: each time this game's player's ragdoll was added to the world, with the
// caller's RVA, and each pre-physics pass that found it out of the world and put it back (TestHarness::RagdollWatch).
nlohmann::json RagdollAdds(Harness&, const nlohmann::json& acRequest)
{
    auto adds = nlohmann::json::array();
    for (const auto& cAdd : RagdollWatch::Adds(acRequest.value("clear", false)))
        adds.push_back({{"tick", cAdd.Tick}, {"caller", std::format("{:X}", 0x140000000 + cAdd.CallerRva)}});
    auto readds = nlohmann::json::array();
    for (const auto& cReadd : RagdollWatch::PrePhysicsReadds(acRequest.value("clear", false)))
        readds.push_back(cReadd.Tick);
    return {{"ok", true}, {"adds", adds}, {"pre_physics_readds", readds}, {"now", GetTickCount64()}};
}

// {"cmd": "ragdoll_out", "queued": false}: the player's ragdoll out of the world now (TestHarness::RagdollWatch::TakeOut).
nlohmann::json RagdollOut(Harness&, const nlohmann::json& acRequest)
{
    return RagdollWatch::TakeOut(acRequest.value("queued", false)) ? Ok() : Error("no player or no graph");
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

// {"cmd": "body_velocity", "target": id, "velocity": [x, y, z]}: the reference's first body woken and moving so
// (game units/s): a known push for a scenario, where the console's ApplyHavokImpulse is fixed by the body's mass.
nlohmann::json BodyVelocity(Harness&, const nlohmann::json& acRequest)
{
    const auto cVelocity = acRequest.value("velocity", std::array<float, 3>{});
    return EngineCalls::SetBodyVelocity(acRequest.value("target", 0u), cVelocity.data()) ? Ok() : Error("no dynamic body");
}

// {"cmd": "clear_map_marker"}: removes the player's map marker in this game only (outside the map, nothing is sent).
nlohmann::json ClearMapMarker(Harness&, const nlohmann::json&)
{
    EngineCalls::ClearPlayerMapMarker();
    return Ok();
}

// {"cmd": "wake", "ref": id}: wakes the reference's bodies where they are (EngineCalls::WakeBodies): staged in the
// air by the console, it falls and settles as a dropped object does.
nlohmann::json Wake(Harness&, const nlohmann::json& acRequest)
{
    return EngineCalls::WakeBodies(acRequest.value("ref", 0u)) ? Ok() : Error("no such reference, or no body to wake");
}

constexpr Command cCommands[] = {
    {"console", true, &Console}, {"console_result", false, &ConsoleResult}, {"menu", false, &Menu}, {"menu_flags", false, &MenuFlags}, {"first_person", true, &FirstPerson}, {"third_person", true, &ThirdPerson},
    {"strip", true, &Strip},     {"reset_worn", true, &ResetWorn}, {"reset_3d", true, &Reset3DCommand},     {"face", true, &Face},  {"place", true, &Place}, {"swing_cast", true, &SwingCast}, {"aim_swing", true, &AimSwing}, {"last_swing", false, &LastSwing}, {"ragdoll_adds", false, &RagdollAdds}, {"ragdoll_out", false, &RagdollOut},
    {"freeze", true, &Freeze},   {"move_actor", true, &MoveActor}, {"put_package", true, &PutPackage}, {"copy_do_nothing", true, &CopyDoNothing}, {"put_on_mount", true, &PutOnMount}, {"board_ridden", true, &BoardRidden}, {"seat_on_mount", true, &SeatOnMountCommand}, {"sit_on_mount_quick", true, &SitOnMountQuickCommand}, {"take_reins", true, &TakeReinsCommand}, {"graph_event", true, &GraphEventCommand}, {"activate_text", true, &ActivateTextCommand}, {"clear_map_marker", true, &ClearMapMarker},
    {"wake", true, &Wake},
    {"body_velocity", true, &BodyVelocity},
};
} // namespace

std::span<const Command> StagingCommands()
{
    return cCommands;
}
} // namespace TestHarness
