#include "Commands.h"
#include "Status.h"
#include "EngineCalls.h"

#include <World.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>
#include <Services/SteamService.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Services/SessionService.h>
#include <Services/BugReportService.h>
#include <Structs/SystemQuestState.h>
#include <Bridges/IJoinBridge.h>
#include <Bridges/IPlayerBridge.h>

// The session: leaving, kicking, and swapping in a rebuilt plugin. Hosting and joining are driven by
// session.txt (Session.h).
namespace TestHarness
{
namespace
{
// {"cmd": "disconnect"}: leaves the session like a player quitting (join again with a new session
// request). The host is the session: when it leaves, the session ends for everyone.
nlohmann::json Disconnect(Harness&, const nlohmann::json&)
{
    auto& world = World::Get();
    if (world.GetHostService().IsHosting())
        world.GetHostService().StopHosting();
    else
        world.GetTransport().Close();
    return Ok();
}

// {"cmd": "kick", "player": id}: the host removes a player from the session.
nlohmann::json Kick(Harness&, const nlohmann::json& acRequest)
{
    auto& host = World::Get().GetHostService();
    if (!host.IsHosting())
        return Error("not hosting");
    host.KickPlayer(acRequest.value("player", 0u));
    return Ok();
}

// {"cmd": "difficulty", "value": 0-5}: sets the player's difficulty, as the settings menu does;
// answers with the difficulty right after (before the session's own update can change it).
nlohmann::json Difficulty(Harness&, const nlohmann::json& acRequest)
{
    EngineCalls::ChooseDifficulty(acRequest.value("value", 2u));
    return {{"ok", true}, {"difficulty", World::Get().GetPlayerBridge().GetDifficulty()}};
}

// {"cmd": "reload"}: the loader swaps in the current build next frame (Code/hotreload/Loader.cpp).
nlohmann::json Reload(Harness&, const nlohmann::json&)
{
    const HMODULE cLoader = GetModuleHandleW(L"SkyrimCoopLoader.dll");
    const auto pRequest = cLoader ? reinterpret_cast<void (*)()>(GetProcAddress(cLoader, "SkyrimCoopLoader_RequestReload")) : nullptr;
    if (!pRequest)
        return Error("not loaded by SkyrimCoopLoader");
    pRequest();
    return {{"ok", true}, {"reload", ReloadStatus()}};
}

// {"cmd": "steam_host"}: makes a Steam lobby and listens, as the Host button will.
nlohmann::json SteamHost(Harness&, const nlohmann::json&)
{
    return World::Get().ctx().at<SteamService>().Host() ? Ok() : Error("Steam not ready, or a session under way");
}

// {"cmd": "steam_invite", "friend": "<SteamID64>"}: the host invites a friend to its lobby, as the overlay's invite does.
nlohmann::json SteamInvite(Harness&, const nlohmann::json& acRequest)
{
    const uint64_t cFriend = std::stoull(acRequest.value("friend", std::string("0")));
    return World::Get().ctx().at<SteamService>().Invite(cFriend) ? Ok() : Error("not hosting a lobby, or Steam refused");
}

// {"cmd": "steam_leave"}: leaves the Steam session (the host's ends it for everyone in it).
nlohmann::json SteamLeave(Harness&, const nlohmann::json&)
{
    World::Get().ctx().at<SteamService>().Leave();
    return Ok();
}

nlohmann::json LeaveSession(Harness&, const nlohmann::json&)
{
    World::Get().GetSessionService().End();
    return Ok();
}

// {"cmd": "bug_report", "text": "..."}: a player's /bug from this game: every game writes its part of the report.
nlohmann::json BugReport(Harness&, const nlohmann::json& acRequest)
{
    World::Get().ctx().at<BugReportService>().Report(acRequest.value("text", std::string{}));
    return Ok();
}

// {"cmd": "join_world", "port": N}: joins the session on that port with the host's world, from this game (its character
// comes along) or from the main menu, as the join screen will. The host across a network namespace link
// (SKYRIMCOOP_JOIN_HOST), loopback otherwise.
nlohmann::json JoinWorld(Harness&, const nlohmann::json& acRequest)
{
    const char* pHost = std::getenv("SKYRIMCOOP_JOIN_HOST");
    const std::string cEndpoint = std::format("{}:{}", pHost && *pHost ? pHost : "127.0.0.1", acRequest.value("port", 0u));
    return World::Get().ctx().at<JoinService>().Join(cEndpoint) ? Ok() : Error("joining already, or in a session");
}

nlohmann::json Describe(const PlayerRecord& acRecord)
{
    nlohmann::json ingredients = nlohmann::json::object(), enchantments = nlohmann::json::array();
    for (const auto& cIngredient : acRecord.Knowledge.Ingredients)
        ingredients[std::format("{:X}:{:X}", cIngredient.Id.ModId, cIngredient.Id.BaseId)] = cIngredient.Effects;
    for (const auto& cEnchantment : acRecord.Knowledge.Enchantments)
        enchantments.push_back(std::format("{:X}:{:X}", cEnchantment.ModId, cEnchantment.BaseId));
    nlohmann::json perks = nlohmann::json::array(), spells = nlohmann::json::array(), skills = nlohmann::json::array();
    for (const auto& cPerk : acRecord.Perks)
        perks.push_back(std::format("{:X}:{:X}", cPerk.Id.ModId, cPerk.Id.BaseId));
    for (const auto& cSpell : acRecord.Spells)
        spells.push_back(std::format("{:X}:{:X}", cSpell.ModId, cSpell.BaseId));
    nlohmann::json words = nlohmann::json::array();
    for (const auto& cWord : acRecord.Words)
        words.push_back(std::format("{:X}:{:X}", cWord.ModId, cWord.BaseId));
    for (const auto& cSkill : acRecord.Skills)
        skills.push_back(cSkill.Level);
    nlohmann::json values = nlohmann::json::object();
    for (const auto& cValue : acRecord.Values)
        values[std::to_string(cValue.Index)] = cValue.Base;
    nlohmann::json permanent = nlohmann::json::object();
    for (const auto& cValue : acRecord.PermanentValues)
        permanent[std::to_string(cValue.Index)] = cValue.Base;
    nlohmann::json tints = nlohmann::json::array();
    for (const auto& cTint : acRecord.FaceTints.Entries)
        tints.push_back({{"name", cTint.Name}, {"type", cTint.Type}, {"color", cTint.Color}, {"alpha", cTint.Alpha}});
    nlohmann::json items = nlohmann::json::object();
    for (const auto& cItem : acRecord.Items.Entries)
    {
        const std::string cId = std::format("{:X}:{:X}", cItem.BaseId.ModId, cItem.BaseId.BaseId);
        items[cId] = items.value(cId, 0) + cItem.Count;
    }
    // Its own system quests' instances (docs/design/system-quests.md): numbers, bools and the registrations' natives.
    nlohmann::json systemQuests = nlohmann::json::object();
    if (const auto cExtra = acRecord.Extras.find(SystemQuestState::kExtra); cExtra != acRecord.Extras.end())
        if (const auto cState = SystemQuestState::Decode(cExtra->second))
            for (const auto& cQuest : cState->Quests)
            {
                nlohmann::json variables = nlohmann::json::object();
                for (const auto& cVariable : cQuest.Variables)
                    std::visit(
                        [&]<class T>(const T& acValue) {
                            if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, float> || std::is_same_v<T, bool>)
                                variables[cVariable.Name] = acValue;
                        },
                        cVariable.Value);
                nlohmann::json timers = nlohmann::json::array();
                for (const auto& cTimer : cQuest.Timers)
                    timers.push_back(cTimer.Native);
                systemQuests[std::format("{:X}:{:X}", cQuest.Id.ModId, cQuest.Id.BaseId)] = {{"variables", variables}, {"timers", timers}};
            }
    // Its player-made potions' recipes, by their session ids (crafted-potions.md).
    nlohmann::json recipes = nlohmann::json::array();
    for (const Recipe& cRecipe : acRecord.Recipes)
        recipes.push_back(cRecipe.Id().BaseId);
    return {{"key", acRecord.Key}, {"name", acRecord.Name}, {"level", acRecord.Level}, {"base_bytes", acRecord.Base.size()},
            {"perk_points", acRecord.PerkPoints}, {"perks", perks}, {"spells", spells}, {"skills", skills}, {"values", values},
            {"permanent_values", permanent}, {"items", items}, {"tints", tints}, {"weight", acRecord.Weight}, {"words", words},
            {"ingredient_effects", ingredients}, {"enchantments", enchantments}, {"knowledge_present", acRecord.Knowledge.Present},
            {"system_quests", systemQuests}, {"recipes", recipes},
            // Where a kept character arrives (JoinBridge::ApplyPlayer; none: the save's player spot).
            {"cell", std::format("{:X}:{:X}", acRecord.Cell.ModId, acRecord.Cell.BaseId)},
            {"worldspace", std::format("{:X}:{:X}", acRecord.WorldSpace.ModId, acRecord.WorldSpace.BaseId)},
            {"position", acRecord.Position}};
}

// {"cmd": "player_record"}: this game's player's character as the join flow reads it (IJoinBridge::ReadPlayer), and
// the key it joins under.
nlohmann::json PlayerRecordOf(Harness&, const nlohmann::json&)
{
    auto& world = World::Get();
    PlayerRecord record = world.ctx().at<IJoinBridge&>().ReadPlayer();
    record.Knowledge = world.ctx().at<IJoinBridge&>().ReadKnowledge();
    record.Key = world.ctx().at<JoinService>().PlayerKey();
    return {{"ok", true}, {"record", Describe(record)}};
}

// {"cmd": "records"}: the characters this game's loaded save keeps (and, on a host, its guests' since).
nlohmann::json Records(Harness&, const nlohmann::json&)
{
    nlohmann::json records = nlohmann::json::array();
    for (const auto& [cKey, cRecord] : World::Get().ctx().at<SaveService>().GetRecords().All())
        records.push_back(Describe(cRecord));
    return {{"ok", true}, {"records", records}};
}

// {"cmd": "forget_record", "key": "..."}: the host's game no longer keeps that player's character (a fresh save for it).
nlohmann::json ForgetRecord(Harness&, const nlohmann::json& acRequest)
{
    World::Get().ctx().at<SaveService>().GetRecords().Remove(acRequest.value("key", std::string()));
    return Ok();
}

constexpr Command cCommands[] = {
    {"forget_record", false, &ForgetRecord},
    {"join_world", false, &JoinWorld},
    {"player_record", false, &PlayerRecordOf},
    {"records", false, &Records},
    {"steam_host", false, &SteamHost},
    {"steam_leave", false, &SteamLeave},
    {"leave_session", false, &LeaveSession},
    {"bug_report", false, &BugReport},
    {"steam_invite", false, &SteamInvite},
    {"disconnect", false, &Disconnect},
    {"kick", false, &Kick},
    {"difficulty", false, &Difficulty},
    {"reload", false, &Reload},
};
} // namespace

std::span<const Command> SessionCommands()
{
    return cCommands;
}
} // namespace TestHarness
