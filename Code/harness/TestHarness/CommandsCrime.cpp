#include <StealthEye.h>
#include "TestHarness/Commands.h"

#include <Actor.h>
#include <AI/AITimer.h>
#include <Combat/CombatManager.h>
#include <AI/AIProcess.h>
#include <ActorExtension.h>
#include <Crime.h>
#include <core/FormIds.h>
#include <Forms/TESFaction.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESPackage.h>
#include <ExtraData/ExtraOwnership.h>
#include <TES.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <crime/CrimeService.h>
#include <VersionDb.h>
#include <core/World.h>

#include <limits>

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
        if (!Cast<Actor>(target))
            return Error("missing victim");
        // As AttemptPickpocket (40654) raises a caught try: no item, count 0.
        using TPick = void(PlayerCharacter*, TESObjectREFR*, TESForm*, int32_t);
        static VersionDbPtr<TPick> pick(37423);
        pick(player, target, nullptr, 0);
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
    else if (action == "lock_unlock")
    {
        // The pick's success: LockpickingMenu::Unlock (51968) unlocks the target and activates it.
        auto* ui = UI::Get();
        auto* menu = ui ? ui->FindMenuByName("Lockpicking Menu") : nullptr;
        if (!menu)
            return Error("no lockpicking menu");
        using TUnlock = void(void*);
        static VersionDbPtr<TUnlock> unlock(51968);
        unlock(menu);
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
        faction->SetPlayerEnemy(aRequest.value("enemy", false));
    }
    else if (action == "actor_state")
    {
        if (!actor || !faction)
            return Error("missing actor or faction");
        using TGet = uint32_t(Actor*, TESFaction*);
        auto get = reinterpret_cast<TGet**>(*reinterpret_cast<uintptr_t**>(actor))[0xB8];
        const auto* own = reinterpret_cast<const CrimeEngine::Faction*>(actor->GetCrimeFaction());
        auto* high = actor->currentProcess ? actor->currentProcess->highProcess : nullptr;
        const auto* cPackage = actor->currentProcess ? actor->currentProcess->currentPackage.package : nullptr;
        return {{"ok", true}, {"bounty", get(actor, faction)}, {"attack_on_sight", (actor->flags2 & 0x8000) != 0},
                {"angry", (actor->flags2 & 0x800) != 0}, {"pending_crime", high && high->pendingCrime},
                {"faction_enemy", own && (own->Flags & 0x8) != 0},
                // The package on the process (+0x20): 21 an alarm, 34 a steal warning. The player's stole-today flag
                // (+0xE8 bit 0x8000, StealAlarm 37422) is what makes a theft's owner attack (39651).
                {"package_type", cPackage ? cPackage->packType : 0xFF},
                {"stole_today", (player->flags1 & 0x8000) != 0},
                {"party_criminal", World::Get().ctx().at<CrimeService>().PartyIsCriminalTo(actor->formID)}};
    }
    else if (action == "set_warnings")
    {
        reinterpret_cast<CrimeEngine::Player*>(player)->TheftWarnings = aRequest.value("warnings", 0u);
    }
    else if (action == "set_angry")
    {
        // kAngryWithPlayer as a save carries it (37463, crime.md).
        if (!actor)
            return Error("missing actor");
        using TSet = void(Actor*, bool);
        static VersionDbPtr<TSet> set(37463);
        set(actor, aRequest.value("angry", true));
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
        auto* crimeFaction = actor->GetCrimeFaction();
        return {{"ok", true}, {"reaction", actor->GetFactionReaction(other)}, {"hostile", actor->IsHostileToActor(other)},
                {"crime_faction", crimeFaction ? crimeFaction->formID : 0}};
    }
    else if (action == "known_crime")
    {
        if (!actor)
            return Error("missing criminal");
        auto* witness = Cast<Actor>(TESForm::GetById(aRequest.value("witness", 0u)));
        return {{"ok", true}, {"known", ProcessLists::Get()->HasKnownCrime(target, actor, witness, aRequest.value("type", 0u), 0, -1)}};
    }
    else if (action == "stealth_meter")
    {
        // The HUD's eye: the highest detection level any actor in high process has of the player (41408), or of
        // `actor` (on the host, a guest's copy: what the host's NPCs see of that guest); in combat it shows 100 minus
        // the lowest stealth points (46877).
        auto* eyeOf = actor ? actor : player;
        uint32_t seeing = 0;
        int32_t cLevel = ProcessLists::Get()->RequestHighestDetectionLevelAgainstActor(eyeOf, seeing);
        auto* pCombatManager = CombatManager::Get();
        float cStealth = pCombatManager ? pCombatManager->GetLowestStealthPoints(eyeOf) : 100.f;
        // Another actor (a guest's copy): what the host sends that guest (HostEye), the player's exclusions applied.
        if (eyeOf != player)
        {
            const auto cReading = StealthEye::Read(*eyeOf);
            cLevel = cReading.Level;
            seeing = cReading.Seeing;
            cStealth = cReading.Stealth;
        }
        // Each high actor's last state of the player, without asking (39763): who holds the eye's level.
        using TFind = CrimeEngine::ActorKnowledge*(HighProcessData*, Actor*);
        static VersionDbPtr<TFind> find(39763);
        nlohmann::json sources = nlohmann::json::array();
        const auto& handles = ProcessLists::Get()->highActorHandleArray;
        for (uint32_t i = 0; i < handles.length; ++i)
        {
            auto* witness = Cast<Actor>(TESObjectREFR::GetByHandle(handles[i]));
            auto* high = witness && witness->currentProcess ? witness->currentProcess->highProcess : nullptr;
            const auto* known = high ? find(high, eyeOf) : nullptr;
            if (!known || !known->State)
                continue;
            sources.push_back({{"actor", witness->formID}, {"level", known->State->Level}, {"seen", known->State->Seen},
                               {"age", AITimer::GetAITime() - known->State->UpdatedAt},
                               {"queued", known->RequestedAt != -std::numeric_limits<float>::max()},
                               {"other_player", witness->GetExtension()->IsOtherPlayer()},
                               {"not_on_meter", (witness->flags2 & Actor::kDoNotShowOnStealthMeter) != 0}});
        }
        return {{"ok", true}, {"level", cLevel}, {"seeing", seeing}, {"stealth", cStealth}, {"in_combat", eyeOf->IsInCombat()},
                {"sources", sources}};
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
    else if (action == "knowledge")
    {
        // What the witness last computed of the target, without requesting an update (39763 finds, never creates).
        auto* other = Cast<Actor>(target);
        if (!actor || !other)
            return Error("missing witness or target");
        using TFind = CrimeEngine::ActorKnowledge*(HighProcessData*, Actor*);
        static VersionDbPtr<TFind> find(39763);
        auto* high = actor->currentProcess ? actor->currentProcess->highProcess : nullptr;
        const auto* known = high ? find(high, other) : nullptr;
        const bool cNotOnMeter = (actor->flags2 & Actor::kDoNotShowOnStealthMeter) != 0; // 41408 skips it
        if (!known)
            return {{"ok", true}, {"known", false}, {"high", high != nullptr}, {"not_on_meter", cNotOnMeter}};
        nlohmann::json result{{"ok", true}, {"known", true}, {"high", true}, {"not_on_meter", cNotOnMeter},
                              {"queued", known->RequestedAt != -std::numeric_limits<float>::max()}};
        if (known->State)
        {
            result["level"] = known->State->Level;
            result["age"] = AITimer::GetAITime() - known->State->UpdatedAt;
        }
        return result;
    }
    else if (action == "owns")
    {
        // PickUpObject's test (37521): not an owner (20210) means a theft.
        if (!actor || !target)
            return Error("missing actor or target");
        auto* owner = target->GetOwner();
        return {{"ok", true}, {"owns", target->IsAnOwner(actor, true, false)}, {"owner", owner ? owner->formID : 0}};
    }
    else if (action == "set_cell_owner")
    {
        auto* cell = player->GetParentCellEx();
        if (!cell)
            return Error("no cell");
        cell->SetOwner(TESForm::GetById(aRequest.value("owner", 0u)));
    }
    else if (action == "trespass_state")
    {
        if (!actor)
            return Error("missing actor");
        auto* cell = actor->GetParentCellEx();
        auto* ownership = cell ? static_cast<ExtraOwnership*>(cell->extraData.GetByType(ExtraDataType::Ownership)) : nullptr;
        return {{"ok", true}, {"flag", (actor->flags2 & 0x1000) != 0},
                {"owner", ownership && ownership->owner ? ownership->owner->formID : 0},
                {"trespass", cell && cell->IsTrespassing(player)}};
    }
    else if (action == "reset_alarm")
    {
        if (!actor)
            return Error("missing actor");
        ProcessLists::Get()->StopCombatAndAlarmOnActor(actor, false);
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
    const auto* state = reinterpret_cast<const CrimeEngine::Player*>(player);
    return {{"ok", true}, {"bounty", rows}, {"reporting", (state->Flags4 & 4) != 0}, {"warnings", state->TheftWarnings}};
}
} // namespace

std::span<const Command> CrimeCommands()
{
    static const Command commands[] = {{"crime", true, &Crime}};
    return commands;
}
} // namespace TestHarness
