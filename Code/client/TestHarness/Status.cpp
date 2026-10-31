#include "Status.h"
#include "EngineCalls.h"
#include "Harness.h"
#include "Session.h"

#include <Diagnostics/Snapshot.h>
#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Components.h>

namespace TestHarness
{
nlohmann::json ReloadStatus()
{
    const HMODULE cLoader = GetModuleHandleW(L"SkyrimCoopLoader.dll");
    if (!cLoader)
        return nullptr;
    const auto pGeneration = reinterpret_cast<uint32_t (*)()>(GetProcAddress(cLoader, "SkyrimCoopLoader_Generation"));
    const auto pLastError = reinterpret_cast<const char* (*)()>(GetProcAddress(cLoader, "SkyrimCoopLoader_LastError"));
    if (!pGeneration || !pLastError)
        return nullptr;
    return {{"generation", pGeneration()}, {"error", pLastError()}};
}

static nlohmann::json PlayerStatus(const Harness& acHarness)
{
    auto& actors = World::Get().GetActorBridge();
    if (!actors.ActorHas3D(0x14))
        return nullptr;
    float position[3]{}, rotation[3]{};
    actors.GetActorPosition(0x14, position[0], position[1], position[2]);
    actors.GetActorRotation(0x14, rotation[0], rotation[1], rotation[2]);
    return {{"position", {position[0], position[1], position[2]}}, {"rotation", {rotation[0], rotation[1], rotation[2]}},
            {"loaded_actors", actors.GetLoadedActorFormIds()}, {"equipped", Diagnostics::EquippedItems(0x14)},
            {"weapon_drawn", actors.IsWeaponDrawn(0x14)}, {"casting", Diagnostics::CastingSpells(0x14)},
            {"selected_power", EngineCalls::GetSelectedPower(0x14)},
            // MagicCaster::State per casting source: 3 once a spell is charged (a release casts it).
            {"caster_states", {EngineCalls::GetCasterState(0x14, 0), EngineCalls::GetCasterState(0x14, 1), EngineCalls::GetCasterState(0x14, 2),
                               EngineCalls::GetCasterState(0x14, 3)}},
            // The draw or sheathe animation still playing: the game ignores Ready Weapon until it ends.
            {"equipping", EngineCalls::GetGraphBool(0x14, "IsEquipping")}, {"unequipping", EngineCalls::GetGraphBool(0x14, "IsUnequipping")},
            {"equip_animations", acHarness.Input.EquipAnimations()},
            // 0 on the ground, 2 in the air (a move with SetPosition leaves the player falling for a moment).
            {"character_state", EngineCalls::GetCharacterState(0x14)},
            // What it holds with Activate (a grabbed physics object), 0 for nothing.
            {"grabbed", EngineCalls::GetGrabbedObject()},
            {"health", actors.GetActorValue(0x14, 24 /* kHealth */)}, {"local_actors", Diagnostics::LocalActors()},
            {"enemy_health_target", EngineCalls::GetEnemyHealthTarget()}};
}

nlohmann::json BuildStatus(const Harness& acHarness)
{
    const auto cUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto world = Diagnostics::WorldSnapshot();
    for (auto it = world.begin(); it != world.end(); ++it)
    {
        if (const auto runtime = EngineCalls::ReadActorRuntime(static_cast<uint32_t>(std::stoul(it.key()))))
            it.value()["mod"]["runtime"] = {{"life_state", runtime->LifeState}, {"state1", runtime->State1}, {"state2", runtime->State2},
                {"native_scene", runtime->NativeScene}, {"owner_scene", runtime->OwnerScene}, {"effective_scene", runtime->EffectiveScene}};
        if (it.value().value("role", std::string{}) == "copy")
            it.value()["mod"]["replay_phase"] = EngineCalls::GetReplayPhase(static_cast<uint32_t>(std::stoul(it.key())));
        if (const auto visibility = EngineCalls::GetPoseVisibility(static_cast<uint32_t>(std::stoul(it.key()))))
            it.value()["mod"]["pose_visibility"] = {{"was_in_frustum", visibility->WasInFrustum},
                                                    {"culling_state", visibility->CullingState}, {"last_seen", visibility->LastSeen}};
    }
    const auto cAction = [](const ActionEvent& acAction) -> nlohmann::json {
        return {{"tick", acAction.Tick}, {"action", acAction.ActionId}, {"idle", acAction.IdleId},
                {"event", acAction.EventName}, {"graph_bytes", acAction.GraphState.size()}};
    };
    auto& game = World::Get();
    for (auto&& [entity, form] : game.view<FormIdComponent>().each())
    {
        const auto it = world.find(std::to_string(form.Id));
        if (it == world.end()) continue;
        auto& mod = it.value()["mod"];
        mod["engine_copy"] = game.GetActorBridge().IsActorCopy(form.Id);
        if (const auto* local = game.try_get<LocalComponent>(entity))
        {
            mod["animation_sent"] = cAction(local->CurrentAction);
            if (const auto* animation = game.try_get<LocalAnimationComponent>(entity))
                mod["animation_pending"] = animation->Actions.size();
        }
        if (const auto* animation = game.try_get<RemoteAnimationComponent>(entity))
        {
            mod["animation_last_taken"] = cAction(animation->LastRanAction);
            mod["animation_head"] = animation->TimePoints.empty() ? nlohmann::json{} : cAction(animation->TimePoints.front());
            mod["animation_tail"] = animation->TimePoints.empty() ? nlohmann::json{} : cAction(animation->TimePoints.back());
            mod["animation_staged_updates"] = animation->StagedUpdates;
            mod["animation_spawn_pending"] = game.all_of<SpawnGraphStateComponent>(entity);
        }
    }
    return {{"pid", GetCurrentProcessId()},
            {"heartbeat_ms", cUnixMs},
            {"actors", Diagnostics::Copies()},
            {"copy_clock", {{"network_ms", World::Get().GetCopyClock().Network()}, {"delay_ms", World::Get().GetCopyClock().Delay()}}},
            {"line", Diagnostics::LineStatus()},
            {"session", PollSession(acHarness.Files.Directory())},
            {"boot", {{"command", acHarness.Files.BootCommand()}, {"state", acHarness.Files.BootState()}}},
            {"console_id", acHarness.Files.ConsoleId()},
            {"player", PlayerStatus(acHarness)},
            {"world", std::move(world)},
            {"game", Diagnostics::GameStatus()},
            {"menus", EngineCalls::GetOpenMenus()},
            {"subtitle_on_screen", EngineCalls::GetSubtitleOnScreen()},
            {"kill_cam", EngineCalls::IsKillCamPlaying()},
            {"clock_speed", EngineCalls::GetGlobalTimeMultiplier()},
            {"lock_order", Diagnostics::LockOrderStatus()},
            {"reload", ReloadStatus()},
            {"frames", Diagnostics::FrameStatus()},
            {"steam", Diagnostics::SteamStatus()}};
}
} // namespace TestHarness
