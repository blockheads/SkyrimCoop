#include <TiltedOnlinePCH.h>
#include "Runtime.h"
#include "Session.h"
#include <Steam/Session.h>
#include <World.h>
#include <Components.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Games/Skyrim/Interface/UI.h>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <Games/Skyrim/BSCore/LockOrder.h>

namespace CoopReload
{
std::string InstanceSuffix()
{
    // SKYRIMCOOP_INSTANCE=client keeps two games in one install apart (control files, logs).
    char instance[32]{};
    const DWORD length = GetEnvironmentVariableA("SKYRIMCOOP_INSTANCE", instance, sizeof(instance));
    if (length == 0 || length >= sizeof(instance))
        return {};
    const std::string name(instance);
    const bool valid = std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
    return valid ? "-" + name : std::string{};
}

Runtime& Runtime::Get()
{
    // Process-lifetime. Unload from EndMain, never from CRT destructors/DllMain.
    static Runtime* runtime = new Runtime;
    return *runtime;
}

void Runtime::Configure(const std::filesystem::path& pluginDirectory)
{
    m_directory = pluginDirectory / ("SkyrimCoopHotReload" + InstanceSuffix());
}

// Console::ExecuteCommand(const char*): logs, compiles and runs the line like the console.
static void ExecuteConsoleCommand(const char* apCommand)
{
    using TExecuteCommand = void(const char*);
    POINTER_SKYRIMSE(TExecuteCommand, s_executeCommand, 442672);
    s_executeCommand.Get()(apCommand);
}

// boot.txt holds one console command (e.g. `coc QASmoke`) run once the main menu is up.
// The engine only runs the INI's sStartingConsoleCommand on shutdown in 1.6.1170.
void Runtime::PollBootCommand()
{
    if (m_bootState == "none")
    {
        std::ifstream bootFile(m_directory / "boot.txt");
        if (!bootFile || !std::getline(bootFile, m_bootCommand) || m_bootCommand.empty())
            return;
        m_bootState = "waiting_for_main_menu";
    }
    if (m_bootState != "waiting_for_main_menu")
        return;

    const auto* pUI = UI::Get();
    if (!pUI || !pUI->GetMenuOpen("Main Menu"))
        return;

    spdlog::info("Boot command: {}", m_bootCommand);
    ExecuteConsoleCommand(m_bootCommand.c_str());
    m_bootState = "executed";
}

// console.txt: "<id> <command>"; each new id runs once (scripts/coop_test.py console).
void Runtime::PollConsoleCommand()
{
    std::ifstream consoleFile(m_directory / "console.txt");
    std::string line;
    if (!consoleFile || !std::getline(consoleFile, line))
        return;
    const auto separator = line.find(' ');
    if (separator == std::string::npos || line.compare(0, separator, m_consoleId) == 0)
        return;
    m_consoleId = line.substr(0, separator);
    spdlog::info("Console command {}: {}", m_consoleId, line.substr(separator + 1));
    ExecuteConsoleCommand(line.c_str() + separator + 1);
}

// Names of the open menus (what blocks input: popups, console, menus).
static nlohmann::json LockOrderStatus()
{
    const auto cStats = LockOrder::GetStats();
    return {{"enabled", cStats.Enabled}, {"edges", cStats.Edges}, {"reports", LockOrder::GetReports()},
            {"report_count", cStats.Reports}, {"too_deep", cStats.TooDeep}, {"table_full", cStats.TableFull}, {"table_resets", cStats.TableResets}};
}

static nlohmann::json OpenMenus()
{
    auto names = nlohmann::json::array();
    if (UI* pUI = UI::Get())
        for (IMenu* pMenu : pUI->menuStack)
            if (const BSFixedString* pName = pUI->LookupMenuNameByInstance(pMenu))
                names.push_back(pName->AsAscii());
    return names;
}

// Tutorial help popups ("Magicka Low", "Health Low"...) swallow all input and console commands
// until closed, which stalls the test harness: close them as soon as they open.
static void CloseTutorialPopups()
{
    UI* pUI = UI::Get();
    if (!pUI || !pUI->GetMenuOpen("Tutorial Menu"))
        return;
    using TAddMessage = void(void*, const BSFixedString*, uint32_t, void*);
    POINTER_SKYRIMSE(TAddMessage, s_addMessage, 13631); // UIMessageQueue::AddMessage
    VersionDbPtr<void*> queue(400445);                   // UIMessageQueue singleton (RVA 0x20F8950)
    if (!*queue.Get())
        return;
    const BSFixedString cName("Tutorial Menu");
    s_addMessage.Get()(*queue.Get(), &cName, UIMessage::kHide, nullptr);
    spdlog::info("Closed a tutorial popup");
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

// Loaded actors this game simulates itself (on the host: every NPC it owns), with health.
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

// Everything this game knows about one actor, for the desync checker (coop_test.py desync): the
// harness pairs an owner's snapshot with its copy's in the other game and reports every field
// that differs. Add a field here and it is checked automatically.
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

static nlohmann::json Snapshot(uint32_t aFormId)
{
    auto& world = World::Get();
    auto& actors = world.GetActorBridge();
    float position[3]{};
    actors.GetActorPosition(aFormId, position[0], position[1], position[2]);

    nlohmann::json mod = {{"server_id", ServerId(aFormId)}};
    // An owner records its last animation on the actor; a copy records the last one it replayed.
    ActionEvent latest = actors.GetActorLatestAnimation(aFormId);
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
            latest = pAnimation->LastRanAction;
            mod["animation_queue"] = pAnimation->TimePoints.size();
            mod["animation_graph_ready"] = actors.IsAnimationGraphReady(aFormId);
        }
        if (const auto* pInterpolation = world.try_get<InterpolationComponent>(entity))
            mod["interpolation_points"] = pInterpolation->TimePoints.size();
        break;
    }

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
                   {"weapon_fully_drawn", actors.IsWeaponFullyDrawn(aFormId)},
                   {"casting", CastingSpells(aFormId)},
                   {"hands", {actors.GetEquippedWeaponFormId(aFormId, 0), actors.GetEquippedWeaponFormId(aFormId, 1)}}}},
        {"values", {{"health", actors.GetActorValue(aFormId, 24)}, {"magicka", actors.GetActorValue(aFormId, 25)},
                    {"stamina", actors.GetActorValue(aFormId, 26)},
                    {"max_health", actors.GetActorPermanentValue(aFormId, 24)},
                    {"max_magicka", actors.GetActorPermanentValue(aFormId, 25)},
                    {"max_stamina", actors.GetActorPermanentValue(aFormId, 26)}}},
        {"equipment", EquippedItems(aFormId)},
        {"effects", ActiveEffects(aFormId)},
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

static nlohmann::json PlayerStatus()
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
            {"health", actors.GetActorValue(0x14, 24 /* kHealth */)}, {"local_actors", LocalActors()}};
}

// One request line from the test harness: {"cmd": "status"} or {"cmd": "console", "line": "..."}.
std::string Runtime::HandleCommand(const std::string& acRequest)
{
    const auto cRequest = nlohmann::json::parse(acRequest, nullptr, false);
    if (cRequest.is_discarded() || !cRequest.is_object())
        return R"({"ok": false, "error": "not a JSON object"})";
    const std::string cCommand = cRequest.value("cmd", "");
    if (cCommand == "status")
        return R"({"ok": true, "status": )" + BuildStatus() + "}";
    if (cCommand == "console")
    {
        if (m_bootState != "executed")
            return R"({"ok": false, "error": "not in the world yet"})";
        const std::string cLine = cRequest.value("line", "");
        spdlog::info("Console command (rpc): {}", cLine);
        ExecuteConsoleCommand(cLine.c_str());
        return R"({"ok": true})";
    }
    if (cCommand == "cast")
    {
        // {"cmd": "cast", "caster": id, "spell": id, "target": id, "source": 1 (right hand)}: casts
        // through the actor's magic caster like a released cast, without aiming.
        if (m_bootState != "executed")
            return R"({"ok": false, "error": "not in the world yet"})";
        World::Get().GetActorBridge().CastSpell(cRequest.value("caster", 0x14u), cRequest.value("spell", 0u),
                                                cRequest.value("source", 1u), cRequest.value("target", 0u));
        return R"({"ok": true})";
    }
    if (cCommand == "interrupt")
    {
        // {"cmd": "interrupt", "caster": id, "source": 1}: ends a held (concentration) cast.
        World::Get().GetActorBridge().InterruptCast(cRequest.value("caster", 0x14u), cRequest.value("source", 1u));
        return R"({"ok": true})";
    }
    return nlohmann::json{{"ok", false}, {"error", "unknown cmd " + cCommand}}.dump();
}

void Runtime::Poll()
{
    if (m_directory.empty()) return;
    if (!m_commandsStarted)
    {
        m_commandsStarted = true;
        if (!m_commands.Start(m_directory.string(), [this](const std::string& acRequest) { return HandleCommand(acRequest); }))
            spdlog::warn("Command server did not start; the test harness falls back to control files");
    }
    m_commands.Poll();

    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextPoll) return;
    m_nextPoll = now + std::chrono::milliseconds(250);
    PollBootCommand();
    if (m_bootState == "executed")
    {
        PollConsoleCommand();
        CloseTutorialPopups();
    }
    std::ifstream requestFile(m_directory / "request.txt");
    if (!requestFile) return; // Opt-in by publishing a module.
    char buffer[128]{};
    requestFile.getline(buffer, sizeof(buffer));
    const std::string request(buffer);
    if (request != m_request)
    {
        m_request = request;
        const bool valid = !requestFile.fail() && !request.empty() && request.size() < 100 &&
            request.find("..") == std::string::npos &&
            std::all_of(request.begin(), request.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            });
        if (request == "builtin")
        {
            m_loader.Unload();
            m_state = "builtin";
            m_error.clear();
            m_observations.clear();
        }
        else if (valid && m_loader.Reload(m_directory / request, m_error))
        {
            m_state = "loaded";
            m_observations.clear();
            spdlog::info("Hot reload: loaded {} (generation {})", m_loader.BuildId(), m_loader.Generation());
        }
        else
        {
            if (!valid) m_error = "invalid module filename";
            m_state = "rejected";
            spdlog::error("Hot reload rejected {}: {}. Previous movement code remains active.", request, m_error);
        }
    }

    const auto temporary = m_directory / "status.json.tmp";
    const auto destination = m_directory / "status.json";
    std::ofstream output(temporary, std::ios::trunc);
    output << BuildStatus(2) << '\n';
    output.close();
    if (output)
        MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::string Runtime::BuildStatus(int aIndent)
{
    const auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // Only copies the mod still links to a server actor: a removed copy must leave the status too.
    std::erase_if(m_observations, [](const auto& aEntry) {
        return ServerId(aEntry.first) < 0 || !World::Get().GetActorBridge().ActorExists(aEntry.first);
    });
    auto actors = nlohmann::json::object();
    for (const auto& [id, observation] : m_observations)
        actors[std::to_string(id)] = {
            {"server_id", ServerId(id)}, {"identity", Identity(id)},
            {"observed_ms", observation.observedMs}, {"generation", observation.generation},
            {"has_3d", observation.has3D},
            {"requested", {observation.requested[0], observation.requested[1], observation.requested[2]}},
            {"actual", {observation.actual[0], observation.actual[1], observation.actual[2]}},
            {"equipped", EquippedItems(id)},
            {"requested_rotation", {observation.requestedRotation[0], observation.requestedRotation[1], observation.requestedRotation[2]}},
            {"rotation", ActorRotation(id)},
            {"remote_player", World::Get().GetActorBridge().IsRemotePlayer(id)},
            {"weapon_drawn", World::Get().GetActorBridge().IsWeaponDrawn(id)},
            {"casting", CastingSpells(id)},
            {"health", World::Get().GetActorBridge().GetActorValue(id, 24 /* kHealth */)},
            {"dead", World::Get().GetActorBridge().IsActorDead(id)}
        };
    nlohmann::json status = {
        {"pid", GetCurrentProcessId()}, {"heartbeat_ms", unixMs},
        {"request", m_request}, {"state", m_state}, {"error", m_error},
        {"build_id", m_loader.BuildId()}, {"generation", m_loader.Generation()},
        {"evaluations", m_loader.Calls()}, {"invalid_outputs", m_loader.InvalidOutputs()},
        {"actors", actors}, {"session", PollSession(m_directory)},
        {"steam", CoopSteam::Poll(m_directory)},
        {"boot", {{"command", m_bootCommand}, {"state", m_bootState}}},
        {"console_id", m_consoleId}, {"player", PlayerStatus()}, {"world", WorldSnapshot()},
        {"menus", OpenMenus()}, {"lock_order", LockOrderStatus()}
    };
    return status.dump(aIndent);
}

void Runtime::Shutdown() { m_commands.Stop(); CoopSteam::Shutdown(); m_loader.Unload(); }
bool Runtime::Evaluate(const CoopMovementInput& input, CoopMovementOutput& output)
{
    return m_loader.Evaluate(input, output);
}
void Runtime::Observe(uint32_t formId, const CoopMovementOutput& requested,
                      const float actual[3], bool has3D)
{
    if (m_directory.empty() || m_request.empty()) return;
    const auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (m_observations.size() >= 128 && m_observations.find(formId) == m_observations.end())
        m_observations.erase(m_observations.begin());
    // Read back from Actor in the engine, never from the interpolation cache.
    m_observations[formId] = {unixMs, m_loader.Generation(), has3D,
        {requested.position[0], requested.position[1], requested.position[2]},
        {actual[0], actual[1], actual[2]},
        {requested.rotation[0], requested.rotation[1], requested.rotation[2]}};
}
}
