#include <TiltedOnlinePCH.h>
#include "Status.h"
#include "EngineCalls.h"
#include "Harness.h"
#include "Session.h"

#include <World.h>
#include <Components.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IWorldBridge.h>
#include <Services/SessionService.h>
#include <Services/TransportService.h>
#include <Services/HostService.h>
#include <Ui/UiService.h>
#include <Games/Skyrim/BSCore/LockOrder.h>

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

// The client's frame timing: frames, stalls, the longest frame and update.
static nlohmann::json FrameStatus()
{
    if (!entt::locator<World>::has_value())
        return {};
    const auto& cStats = World::Get().GetFrameStats();
    return {{"frames", cStats.Frames}, {"stalls", cStats.Stalls}, {"longest_frame_ms", cStats.LongestFrameMs},
            {"longest_update_ms", cStats.LongestUpdateMs}};
}

static nlohmann::json LockOrderStatus()
{
    const auto cStats = LockOrder::GetStats();
    return {{"enabled", cStats.Enabled}, {"edges", cStats.Edges}, {"reports", LockOrder::GetReports()},
            {"report_count", cStats.Reports}, {"too_deep", cStats.TooDeep}, {"table_full", cStats.TableFull}, {"table_resets", cStats.TableResets}};
}

// Game form IDs of the items an actor has equipped (worn armor, weapons, spells).
static nlohmann::json EquippedItems(uint32_t aFormId)
{
    auto& world = World::Get();
    auto items = nlohmann::json::array();
    for (const auto& entry : world.GetFormBridge().GetActorEquipment(aFormId).Entries)
        if (entry.IsWorn())
            items.push_back(world.GetModSystem().GetGameId(entry.BaseId));
    return items;
}

// Spell being cast per casting source: [left hand, right hand, other, instant].
static nlohmann::json CastingSpells(uint32_t aFormId)
{
    auto& actors = World::Get().GetActorBridge();
    return {actors.GetCastingSpellFormId(aFormId, 0), actors.GetCastingSpellFormId(aFormId, 1),
            actors.GetCastingSpellFormId(aFormId, 2), actors.GetCastingSpellFormId(aFormId, 3)};
}

static nlohmann::json ActorRotation(uint32_t aFormId)
{
    float rotation[3]{};
    World::Get().GetActorBridge().GetActorRotation(aFormId, rotation[0], rotation[1], rotation[2]);
    return {rotation[0], rotation[1], rotation[2]};
}

// The server id the mod links this game actor to (-1 if none; 0 is a valid id), so the harness
// can pair an owned actor with its copy in the other game.
static int64_t ServerId(uint32_t aFormId)
{
    auto& world = World::Get();
    for (const auto entity : world.view<FormIdComponent>())
    {
        if (world.get<FormIdComponent>(entity).Id != aFormId)
            continue;
        if (const auto* pLocal = world.try_get<LocalComponent>(entity))
            return pLocal->Id;
        if (const auto* pRemote = world.try_get<RemoteComponent>(entity))
            return pRemote->Id;
    }
    return -1;
}

// What the actor is, to catch a copy built from the wrong record (e.g. a blank custom actor).
static nlohmann::json Identity(uint32_t aFormId)
{
    auto& actors = World::Get().GetActorBridge();
    return {{"base", actors.GetActorBaseFormId(aFormId)}, {"name", actors.GetActorBaseName(aFormId)},
            {"race", actors.GetActorRaceFormId(aFormId)}};
}

// Active magic effects: abilities and worn enchantments are permanent (duration 0), spells and
// hits are timed. Flags: 0x8000 inactive, 0x40000 dispelled.
static nlohmann::json ActiveEffects(uint32_t aFormId)
{
    auto effects = nlohmann::json::array();
    for (const auto& cEffect : World::Get().GetActorBridge().GetActorActiveEffects(aFormId))
        effects.push_back({{"effect", cEffect.EffectFormId}, {"spell", cEffect.SpellFormId},
                           {"caster", cEffect.CasterFormId}, {"magnitude", cEffect.Magnitude},
                           {"duration", cEffect.Duration}, {"elapsed", cEffect.Elapsed}, {"flags", cEffect.Flags}});
    return effects;
}

// What the actor's animation graph holds right now (the variables the animation sync carries),
// as opposed to "animation", which is the last action the mod recorded.
static nlohmann::json GraphVariables(uint32_t aFormId)
{
    AnimationVariables variables;
    World::Get().GetActorBridge().ActorSaveAnimationVariables(aFormId, variables);
    auto bools = nlohmann::json::array();
    for (const bool cValue : variables.Booleans)
        bools.push_back(cValue);
    return {{"booleans", bools},
            {"integers", std::vector<uint32_t>(variables.Integers.begin(), variables.Integers.end())},
            {"floats", std::vector<float>(variables.Floats.begin(), variables.Floats.end())}};
}

// Everything this game knows about one actor, for the desync checker (coop_test.py desync): the
// harness pairs an owner's snapshot with its copy's in the other game and reports every field
// that differs. Add a field here and it is checked automatically.
static nlohmann::json Snapshot(uint32_t aFormId)
{
    auto& world = World::Get();
    auto& actors = world.GetActorBridge();
    float position[3]{};
    actors.GetActorPosition(aFormId, position[0], position[1], position[2]);

    nlohmann::json mod = {{"server_id", ServerId(aFormId)}};
    // The last action the actor's graph took: an owner's own, or a copy's accepted replay or forced action.
    const ActionEvent latest = actors.GetActorLatestAnimation(aFormId);
    for (const auto entity : world.view<FormIdComponent>())
    {
        if (world.get<FormIdComponent>(entity).Id != aFormId)
            continue;
        auto components = nlohmann::json::array();
        const auto has = [&](auto* apType, const char* apName) {
            if (world.all_of<std::remove_pointer_t<decltype(apType)>>(entity))
                components.push_back(apName);
        };
        has(static_cast<LocalComponent*>(nullptr), "Local");
        has(static_cast<RemoteComponent*>(nullptr), "Remote");
        has(static_cast<WaitingForAssignmentComponent*>(nullptr), "WaitingForAssignment");
        has(static_cast<WaitingFor3D*>(nullptr), "WaitingFor3D");
        has(static_cast<InterpolationComponent*>(nullptr), "Interpolation");
        has(static_cast<LocalAnimationComponent*>(nullptr), "LocalAnimation");
        has(static_cast<RemoteAnimationComponent*>(nullptr), "RemoteAnimation");
        has(static_cast<FaceGenComponent*>(nullptr), "FaceGen");
        has(static_cast<CacheComponent*>(nullptr), "Cache");
        has(static_cast<ActorValuesComponent*>(nullptr), "ActorValues");
        has(static_cast<PlayerComponent*>(nullptr), "Player");
        mod["components"] = components;
        if (const auto* pAnimation = world.try_get<RemoteAnimationComponent>(entity))
        {
            mod["animation_queue"] = pAnimation->TimePoints.size();
            mod["animation_graph_ready"] = actors.IsAnimationGraphReady(aFormId);
        }
        if (const auto* pInterpolation = world.try_get<InterpolationComponent>(entity))
            mod["interpolation_points"] = pInterpolation->TimePoints.size();
        break;
    }

    // The voice line, shown and not compared: a copy's starts a little after its owner's, and the
    // callback state can rest at 3 once a line is over (the audio system's late callback).
    const auto cVoiceSound = EngineCalls::GetVoiceSound(aFormId);
    mod["voice"] = {{"callback", EngineCalls::GetVoiceLineState(aFormId)}, {"seconds", EngineCalls::GetVoiceTimer(aFormId)},
                    {"sound", static_cast<int64_t>(static_cast<int32_t>(cVoiceSound.Id))}, {"duration_ms", cVoiceSound.DurationMs}};
    mod["process_level"] = EngineCalls::GetProcessLevel(aFormId);
    mod["process_me"] = EngineCalls::IsProcessMe(aFormId);
    mod["package_type"] = EngineCalls::GetPackageType(aFormId);
    mod["subtitle"] = EngineCalls::GetSubtitle(aFormId);
    // Water, shown and not compared: a copy's height follows its owner's a little late.
    const auto cWater = EngineCalls::GetWater(aFormId);
    mod["water"] = {{"submerged", cWater.Submerged}, {"swimming", cWater.Swimming}, {"head_under", cWater.HeadUnder},
                    {"seconds_under", cWater.SecondsUnder}};

    return {
        {"identity", {{"base", actors.GetActorBaseFormId(aFormId)}, {"name", actors.GetActorBaseName(aFormId)},
                      {"race", actors.GetActorRaceFormId(aFormId)},
                      {"template_base", actors.GetActorNpcTemplateBaseFormId(aFormId)},
                      {"temporary_base", actors.IsActorNpcTemporary(aFormId)},
                      {"mount", actors.IsActorMount(aFormId)}, {"dragon", actors.IsActorDragon(aFormId)}}},
        {"placement", {{"position", {position[0], position[1], position[2]}}, {"rotation", ActorRotation(aFormId)},
                       {"cell", actors.GetActorParentCellFormId(aFormId)},
                       {"worldspace", actors.GetActorWorldSpaceFormId(aFormId)},
                       {"has_3d", actors.ActorHas3D(aFormId)}, {"disabled", actors.IsActorDisabled(aFormId)}}},
        {"state", {{"dead", actors.IsActorDead(aFormId)}, {"bleeding_out", actors.IsBleedingOut(aFormId)},
                   {"weapon_drawn", actors.IsWeaponDrawn(aFormId)},
                   {"weapon_state", static_cast<int>(actors.GetWeaponState(aFormId))},
                   {"sneaking", EngineCalls::IsSneaking(aFormId)},
                   {"sit_sleep", EngineCalls::GetSitSleepState(aFormId)},
                   {"casting", CastingSpells(aFormId)},
                   {"hands", {actors.GetEquippedWeaponFormId(aFormId, 0), actors.GetEquippedWeaponFormId(aFormId, 1)}}}},
        {"values", {{"health", actors.GetActorValue(aFormId, 24)}, {"magicka", actors.GetActorValue(aFormId, 25)},
                    {"stamina", actors.GetActorValue(aFormId, 26)},
                    {"max_health", actors.GetActorPermanentValue(aFormId, 24)},
                    {"max_magicka", actors.GetActorPermanentValue(aFormId, 25)},
                    {"max_stamina", actors.GetActorPermanentValue(aFormId, 26)}}},
        {"equipment", EquippedItems(aFormId)},
        {"effects", ActiveEffects(aFormId)},
        {"visuals", EngineCalls::GetPlayingVisualFormIds(aFormId)},
        {"animation", {{"event", latest.EventName.c_str()}, {"target_event", latest.TargetEventName.c_str()},
                       {"action", latest.ActionId}, {"idle", latest.IdleId},
                       {"state1", latest.State1}, {"state2", latest.State2}}},
        {"graph", GraphVariables(aFormId)},
        {"mod", mod},
        // AI runs only on the owner (a copy's is disabled), so this is shown, not compared.
        {"ai", {{"combat_target", actors.GetCombatTargetFormId(aFormId)}, {"killer", actors.GetKillerFormId(aFormId)}}},
    };
}

// Snapshots of every loaded actor, keyed by this game's form id. "role": "own" for actors this
// game simulates (its player, and on the host every NPC), "copy" for actors mirrored from another.
static nlohmann::json WorldSnapshot()
{
    auto& actors = World::Get().GetActorBridge();
    auto result = nlohmann::json::object();
    std::vector<uint32_t> ids = actors.GetLoadedActorFormIds();
    if (actors.ActorHas3D(0x14))
        ids.push_back(0x14);
    for (const uint32_t cFormId : ids)
    {
        auto snapshot = Snapshot(cFormId);
        snapshot["role"] = actors.IsActorRemote(cFormId) ? "copy" : "own";
        snapshot["player"] = cFormId == 0x14 || actors.IsRemotePlayer(cFormId);
        result[std::to_string(cFormId)] = std::move(snapshot);
    }
    return result;
}

// Loaded actors this game simulates itself (on the host: every NPC it owns), with health.
static nlohmann::json LocalActors()
{
    auto& actors = World::Get().GetActorBridge();
    auto result = nlohmann::json::object();
    for (const uint32_t cFormId : actors.GetLoadedActorFormIds())
        if (!actors.IsActorRemote(cFormId))
        {
            float position[3]{};
            actors.GetActorPosition(cFormId, position[0], position[1], position[2]);
            result[std::to_string(cFormId)] = {{"health", actors.GetActorValue(cFormId, 24)},
                                               {"dead", actors.IsActorDead(cFormId)},
                                               {"position", {position[0], position[1], position[2]}},
                                               {"server_id", ServerId(cFormId)},
                                               {"identity", Identity(cFormId)}};
        }
    return result;
}

// Session-wide state each game holds: its clock, weather, the other players and connection.
static nlohmann::json GameStatus()
{
    auto& world = World::Get();
    const auto& bridge = world.GetWorldBridge();
    nlohmann::json players = nlohmann::json::array();
    for (const auto& [cId, cPlayer] : world.GetSessionService().GetPlayers())
        players.push_back({{"id", cId}, {"name", cPlayer.Name.c_str()}, {"host", cPlayer.IsHost}});
    // The party panel as drawn: null when the UI is not up (no device yet, or no document).
    nlohmann::json party = nullptr;
    if (const auto* pUi = world.ctx().find<UiService>(); pUi && pUi->IsPartyPanelShown())
    {
        party = nlohmann::json::array();
        for (const auto& cMember : pUi->GetPartyMembers())
            party.push_back({{"name", cMember.Name}, {"host", cMember.IsHost}, {"health", cMember.Health}});
    }
    // The chat: every line received, whether its line is open and what is typed in it.
    nlohmann::json chat = nullptr;
    if (const auto* pUi = world.ctx().find<UiService>())
    {
        nlohmann::json lines = nlohmann::json::array();
        for (const auto& cLine : pUi->GetChatLines())
            lines.push_back({{"name", cLine.Name}, {"text", cLine.Text}, {"system", cLine.System}});
        chat = {{"open", pUi->IsChatOpen()}, {"draft", pUi->GetChatDraft()}, {"lines", lines}};
    }
    nlohmann::json cursor = nullptr;
    if (const auto cCursor = EngineCalls::GetMenuCursor())
        cursor = {cCursor->first, cCursor->second};
    // The player's map marker.
    nlohmann::json mapMarker = nullptr;
    if (const auto cMarker = EngineCalls::GetPlayerMapMarker())
        mapMarker = {{"position", {cMarker->X, cMarker->Y, cMarker->Z}}, {"world", cMarker->WorldOrCell}};
    return {{"time", {{"hour", bridge.GetGameTimeHours()}, {"day", bridge.GetGameDay()}, {"month", bridge.GetGameMonth()},
                      {"year", bridge.GetGameYear()}, {"scale", bridge.GetGameTimeScale()}}},
            {"weather", bridge.GetCurrentWeatherId()},
            {"players", players}, {"party_panel", party}, {"chat", chat}, {"map_marker", mapMarker}, {"map_ready", EngineCalls::IsMapReady()}, {"cursor", cursor},
            {"online", world.GetTransport().IsOnline()}, {"hosting", world.GetHostService().IsHosting()}};
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
            {"loaded_actors", actors.GetLoadedActorFormIds()}, {"equipped", EquippedItems(0x14)},
            {"weapon_drawn", actors.IsWeaponDrawn(0x14)}, {"casting", CastingSpells(0x14)},
            // MagicCaster::State per casting source: 3 once a spell is charged (a release casts it).
            {"caster_states", {EngineCalls::GetCasterState(0x14, 0), EngineCalls::GetCasterState(0x14, 1), EngineCalls::GetCasterState(0x14, 2),
                               EngineCalls::GetCasterState(0x14, 3)}},
            // The draw or sheathe animation still playing: the game ignores Ready Weapon until it ends.
            {"equipping", EngineCalls::GetGraphBool(0x14, "IsEquipping")}, {"unequipping", EngineCalls::GetGraphBool(0x14, "IsUnequipping")},
            {"equip_animations", acHarness.Input.EquipAnimations()},
            // 0 on the ground, 2 in the air (a move with SetPosition leaves the player falling for a moment).
            {"character_state", EngineCalls::GetCharacterState(0x14)},
            {"health", actors.GetActorValue(0x14, 24 /* kHealth */)}, {"local_actors", LocalActors()},
            {"enemy_health_target", EngineCalls::GetEnemyHealthTarget()},
            {"combat_skill", {{"skill", EngineCalls::GetCombatSkillExperience().first}, {"xp", EngineCalls::GetCombatSkillExperience().second}}}};
}

// The copies this game shows of other games' actors (players and NPCs), read from the engine: where
// the mod puts each one ("requested") and where the engine has it ("actual").
static nlohmann::json Copies()
{
    auto& world = World::Get();
    auto& actors = world.GetActorBridge();
    const auto cUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto result = nlohmann::json::object();
    for (const auto cEntity : world.view<FormIdComponent, RemoteComponent>())
    {
        const uint32_t cFormId = world.get<FormIdComponent>(cEntity).Id;
        if (!actors.ActorExists(cFormId))
            continue;
        float position[3]{};
        actors.GetActorPosition(cFormId, position[0], position[1], position[2]);
        nlohmann::json copy = {
            {"server_id", world.get<RemoteComponent>(cEntity).Id},
            {"identity", Identity(cFormId)},
            {"observed_ms", cUnixMs},
            {"has_3d", actors.ActorHas3D(cFormId)},
            {"actual", {position[0], position[1], position[2]}},
            {"equipped", EquippedItems(cFormId)},
            {"rotation", ActorRotation(cFormId)},
            {"remote_player", actors.IsRemotePlayer(cFormId)},
            {"weapon_drawn", actors.IsWeaponDrawn(cFormId)},
            {"casting", CastingSpells(cFormId)},
            {"health", actors.GetActorValue(cFormId, 24 /* kHealth */)},
            {"dead", actors.IsActorDead(cFormId)}};
        // Once the mod has moved it (two movement snapshots in).
        if (const auto* pInterpolation = world.try_get<InterpolationComponent>(cEntity); pInterpolation && pInterpolation->TimePoints.size() >= 2)
            copy["requested"] = {pInterpolation->Position.x, pInterpolation->Position.y, pInterpolation->Position.z};
        result[std::to_string(cFormId)] = std::move(copy);
    }
    return result;
}

nlohmann::json BuildStatus(const Harness& acHarness)
{
    const auto cUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return {{"pid", GetCurrentProcessId()},
            {"heartbeat_ms", cUnixMs},
            {"actors", Copies()},
            {"session", PollSession(acHarness.Files.Directory())},
            {"boot", {{"command", acHarness.Files.BootCommand()}, {"state", acHarness.Files.BootState()}}},
            {"console_id", acHarness.Files.ConsoleId()},
            {"player", PlayerStatus(acHarness)},
            {"world", WorldSnapshot()},
            {"game", GameStatus()},
            {"menus", EngineCalls::GetOpenMenus()},
            {"lock_order", LockOrderStatus()},
            {"reload", ReloadStatus()},
            {"frames", FrameStatus()}};
}
} // namespace TestHarness
