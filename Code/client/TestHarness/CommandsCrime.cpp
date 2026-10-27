#include "Commands.h"

#include <Actor.h>
#include <Crime.h>
#include <FormIds.h>
#include <Forms/TESFaction.h>
#include <Forms/TESObjectCELL.h>
#include <ExtraData/ExtraOwnership.h>
#include <Games/TES.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <Services/CrimeService.h>
#include <VersionDb.h>
#include <World.h>

namespace TestHarness
{
namespace
{
// Native entry points, rather than service calls: exercises the same hooks as game actions.
nlohmann::json Crime(Harness&, const nlohmann::json& aRequest)
{
    const auto action = aRequest.value("action", "read");
    auto* player = PlayerCharacter::Get();
    auto* actor = Cast<Actor>(TESForm::GetById(aRequest.value("actor", 0x14u)));
    auto* target = Cast<TESObjectREFR>(TESForm::GetById(aRequest.value("target", 0u)));
    auto* item = TESForm::GetById(aRequest.value("item", 0xFu));
    auto* faction = Cast<TESFaction>(TESForm::GetById(aRequest.value("faction", 0x28170u)));
    const auto count = aRequest.value("count", 1);
    if (!player)
        return Error("no player");
    if (action == "set")
    {
        if (!faction)
            return Error("no faction");
        using TSet = void(PlayerCharacter*, TESFaction*, bool, uint32_t);
        auto set = reinterpret_cast<TSet**>(*reinterpret_cast<uintptr_t**>(player))[0xB5];
        set(player, faction, true, aRequest.value("violent", 0u));
        set(player, faction, false, aRequest.value("nonviolent", 0u));
    }
    else if (action == "theft")
    {
        if (!actor || !target || !item)
            return Error("missing thief, target or item");
        using TSteal = void(Actor*, TESObjectREFR*, TESForm*, int32_t, int32_t, TESForm*, bool);
        static VersionDbPtr<TSteal> steal(37422);
        steal(actor, target, item, count, 0, target->GetOwner(), aRequest.value("warning", false));
    }
    else if (action == "pickpocket")
    {
        if (!Cast<Actor>(target) || !item)
            return Error("missing victim or item");
        using TPick = void(PlayerCharacter*, TESObjectREFR*, TESForm*, int32_t);
        static VersionDbPtr<TPick> pick(37423);
        pick(player, target, item, count);
    }
    else if (action == "assault" || action == "murder")
    {
        auto* victim = Cast<Actor>(target);
        if (!actor || !victim)
            return Error("missing criminal or victim");
        if (action == "assault")
        {
            using TAlarm = void(Actor*, Actor*, bool);
            static VersionDbPtr<TAlarm> alarm(37424);
            alarm(victim, actor, true);
        }
        else
        {
            using TAlarm = void(Actor*, Actor*);
            static VersionDbPtr<TAlarm> alarm(37426);
            alarm(victim, actor);
        }
    }
    else if (action == "lock_check")
    {
        auto* ui = UI::Get();
        auto* menu = ui ? ui->FindMenuByName("Lockpicking Menu") : nullptr;
        if (!menu)
            return Error("no lockpicking menu");
        using TCheck = bool(void*);
        static VersionDbPtr<TCheck> check(51981);
        return {{"ok", true}, {"caught", check(menu)}};
    }
    else if (action == "pay")
    {
        if (!faction)
            return Error("no faction");
        player->PayFine(faction, aRequest.value("jail", false), aRequest.value("stolen", false));
    }
    else if (action == "set_enemy")
    {
        if (!faction)
            return Error("no faction");
        using TSet = void(TESFaction*, bool);
        static VersionDbPtr<TSet> set(24505);
        set(faction, aRequest.value("enemy", false));
    }
    else if (action == "actor_state")
    {
        if (!actor || !faction)
            return Error("missing actor or faction");
        using TGet = uint32_t(Actor*, TESFaction*);
        auto get = reinterpret_cast<TGet**>(*reinterpret_cast<uintptr_t**>(actor))[0xB8];
        return {{"ok", true}, {"bounty", get(actor, faction)}, {"attack_on_sight", (actor->flags2 & 0x8000) != 0}};
    }
    else if (action == "jail_state")
    {
        const auto* state = reinterpret_cast<const CrimeEngine::Player*>(player);
        return {{"ok", true}, {"faction", state->JailFaction ? state->JailFaction->formID : 0},
                {"sentence", state->JailSentence}};
    }
    else if (action == "reaction")
    {
        auto* other = Cast<Actor>(target);
        if (!actor || !other)
            return Error("missing observer or target");
        using TReaction = int32_t(Actor*, Actor*);
        using THostile = bool(Actor*, Actor*);
        static VersionDbPtr<TReaction> reaction(37666);
        static VersionDbPtr<THostile> hostile(37537);
        using TFaction = TESFaction*(Actor*);
        static VersionDbPtr<TFaction> getFaction(37380);
        auto* crimeFaction = getFaction(actor);
        return {{"ok", true}, {"reaction", reaction(actor, other)}, {"hostile", hostile(actor, other)},
                {"crime_faction", crimeFaction ? crimeFaction->formID : 0}};
    }
    else if (action == "known_crime")
    {
        if (!actor)
            return Error("missing criminal");
        auto* witness = Cast<Actor>(TESForm::GetById(aRequest.value("witness", 0u)));
        using TQuery = bool(ProcessLists*, TESObjectREFR*, TESObjectREFR*, Actor*, uint32_t, int32_t, int32_t);
        static VersionDbPtr<TQuery> query(41354);
        return {{"ok", true}, {"known", query(ProcessLists::Get(), target, actor, witness,
                                              aRequest.value("type", 0u), 0, -1)}};
    }
    else if (action == "detection")
    {
        auto* victim = Cast<Actor>(target);
        if (!actor || !victim)
            return Error("missing observer or target");
        using TDetect = int32_t(Actor*, Actor*, uint32_t);
        static VersionDbPtr<TDetect> detect(37764);
        return {{"ok", true}, {"level", detect(actor, victim, 3)}};
    }
    else if (action == "set_cell_owner")
    {
        auto* cell = player->GetParentCellEx();
        if (!cell)
            return Error("no cell");
        using TSet = void(ExtraDataList*, TESForm*);
        static VersionDbPtr<TSet> set(11609);
        set(&cell->extraData, TESForm::GetById(aRequest.value("owner", 0u)));
    }
    else if (action == "trespass_state")
    {
        if (!actor)
            return Error("missing actor");
        using TCheck = bool(TESObjectCELL*, Actor*);
        static VersionDbPtr<TCheck> check(18909);
        auto* cell = actor->GetParentCellEx();
        auto* ownership = cell ? static_cast<ExtraOwnership*>(cell->extraData.GetByType(ExtraDataType::Ownership)) : nullptr;
        return {{"ok", true}, {"flag", (actor->flags2 & 0x1000) != 0},
                {"owner", ownership && ownership->owner ? ownership->owner->formID : 0},
                {"trespass", cell && check(cell, player)}};
    }
    else if (action == "reset_alarm")
    {
        if (!actor)
            return Error("missing actor");
        using TStop = void(ProcessLists*, Actor*, bool);
        static VersionDbPtr<TStop> stop(41340);
        stop(ProcessLists::Get(), actor, false);
    }
    else if (action == "set_owner")
    {
        if (!target)
            return Error("missing target");
        target->SetOwner(TESForm::GetById(aRequest.value("owner", 0u)));
    }
    else if (action != "read")
        return Error("unknown crime action");
    auto rows = nlohmann::json::array();
    for (const auto& row : World::Get().ctx().at<CrimeService>().ReadGold())
        rows.push_back({{"faction", ToGame(row.Faction)}, {"violent", row.Violent}, {"nonviolent", row.NonViolent},
                        {"enemy", row.PlayerEnemy}});
    return {{"ok", true}, {"bounty", rows}};
}
} // namespace

std::span<const Command> CrimeCommands()
{
    static const Command commands[] = {{"crime", true, &Crime}};
    return commands;
}
} // namespace TestHarness
