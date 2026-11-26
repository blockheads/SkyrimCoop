// FakeEngine: spells, effects and their visuals.
#include <CriticalStageRules.h>
#include <animation/DeferredClips.h>
#include <DisplayNames.h>
#include <FreedWornList.h>
#include <Diagnostics/HeadTrackRead.h>
#include <CopyCombat.h>
#include <core/MenuEvent.h>
#include <magic/InterruptCastEvent.h>
#include <characters/ActorHeadUpdateEvent.h>
#include <CheckedPlacement.h>
#include <core/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include <movement/CopyStep.h>
#include <magic/OwnerEffects.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <mounts/MountEvent.h>
#include <join/SaveWrittenEvent.h>
#include <animation/GraphState.h>
#include <animation/GraphStream.h>
#include <bit>
#include <set>
#include <span>
#include <characters/ActorRenamedEvent.h>
#include "FakeDialogue.h"
#include <core/PlayerTravelQueuedEvent.h>
#include <scripts/StoryEvents.h>
#include <items/ContainerChangeEvent.h>
#include <core/DifficultyChangeEvent.h>
#include <world/ActivateEvent.h>
#include <items/ContainerOpens.h>
#include <death/DeathDrops.h>
#include <combat/AddTargetEvent.h>
#include <magic/SummonEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <magic/EffectVisualsChangeEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDamagedEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <characters/NpcHeldEvent.h>
#include <GuestNpcs.h>
#include <actorstate/DeathActions.h>
#include <animation/ReplayCapture.h>
#include <animation/ReplayGraph.h>
#include <characters/NpcSnapshot.h>
#include <combat/HitEvent.h>
#include <animation/ActionRules.h>
#include <magic/EffectRules.h>
#include <magic/ProjectileLaunchedEvent.h>
#include <combat/TrapEvents.h>
#include <combat/TrapRules.h>
#include <magic/SpellCastEvent.h>
#include <items/InventoryChangeEvent.h>
#include <world/LockChangeEvent.h>
#include <items/LooseItemEvents.h>
#include <scripts/CallEndedEvent.h>
#include <scripts/RoutedCallEvent.h>
#include <core/PlayerProgressEvent.h>
#include <core/EngineNames.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>
#include <travel/RemoveWaypointEvent.h>
#include <travel/SetWaypointEvent.h>
#include <time/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <scripts/PartyNatives.h>
#include <scripts/SystemQuests.h>
#include <scripts/SystemQuestLearnedEvent.h>
#include <core/ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <items/Overrides.h>
#include <join/GameLoadedEvent.h>
#include <core/PlayerFormChangeEvent.h>
#include <join/SaveService.h>
#include <join/SaveNames.h>
#include <Wire/Buffer.h>
#include <core/World.h>
#include <core/FormIds.h>

using namespace PartyRules;

void FakeEngine::ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept
{
    if (auto it = Actors.find(aActorFormId); it != Actors.end())
        it->second.Items.CurrentMagicEquipment.Shout = ToSession(aShoutFormId).value_or(GameId{});
}

Inventory FakeEngine::GetActorInventory(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return {};
    // TESObjectREFR::GetInventory: the base container's entries as they are, with the actor's own change of them.
    Inventory inventory = it->second.Items;
    if (const auto cBase = BaseContainers.find(it->second.Base); cBase != BaseContainers.end())
        for (const auto& [cItem, cCount] : cBase->second)
        {
            const auto cOwn = std::ranges::find_if(inventory.Entries, [&](const Inventory::Entry& acEntry) {
                return ::ItemForm(acEntry.BaseId) == cItem && !acEntry.ContainsExtraData();
            });
            if (cOwn != inventory.Entries.end())
                cOwn->Count += cCount;
            else
                inventory.Entries.push_back({.BaseId = GameId(0, cItem), .Count = cCount});
        }
    return inventory;
}

void FakeEngine::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // As AddOrRemoveItem: an item with no form here is not added, a coating with no poison here is lost.
    it->second.Items = acInventory;
    std::erase_if(it->second.Items.Entries, [](const Inventory::Entry& acEntry) { return !::ItemForm(acEntry.BaseId); });
    for (auto& entry : it->second.Items.Entries)
        if (entry.ExtraPoisonId && !::ItemForm(entry.ExtraPoisonId))
            entry.ExtraPoisonId = {}, entry.ExtraPoisonCount = 0;
    EquipSpells(aFormId, acInventory.CurrentMagicEquipment); // Actor::SetMagicEquipment
}

std::vector<IActorBridge::ActiveEffectInfo> FakeEngine::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> effects;
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        for (const uint32_t cId : it->second.Visuals)
            effects.push_back({.EffectFormId = cId});
    for (const auto& cEffect : Effects)
        if (cEffect.Target == aFormId)
            effects.push_back({.EffectFormId = cEffect.EffectId, .SpellFormId = cEffect.Spell, .CasterFormId = cEffect.Caster,
                               .CasterIsActor = Actors.contains(cEffect.Caster)});
    return effects;
}

void FakeEngine::JudgedPlayer(uint32_t aWitness, int32_t aLevel)
{
    KnowledgeOfPlayer[aWitness] = {DetectionRules::Pass{.Level = aLevel, .LineOfSight = true, .Seen = true}, DetectionFrame, false};
}

void FakeEngine::ForcesDetected(uint32_t aWitness)
{
    auto& known = KnowledgeOfPlayer[aWitness];
    const auto cBefore = known.State.value_or(DetectionRules::Pass{});
    known.State = DetectionRules::Pass{.Level = 100, .LineOfSight = cBefore.LineOfSight, .Seen = cBefore.Seen, .Unk16 = 2};
    known.UpdatedAt = DetectionFrame;
}

const DetectionRules::Pass* FakeEngine::FreshState(uint32_t aWitness, uint32_t aTarget)
{
    // Priority 3: refreshed after 1 s, answered unseen after 2 s (table 0x1420B8AB8), at 60 frames a second.
    auto& known = DetectionOf[aTarget][aWitness];
    if (!known.State || known.Queued)
    {
        known.Queued = true;
        return known.State ? &*known.State : nullptr;
    }
    const uint32_t cAge = DetectionFrame - known.UpdatedAt;
    if (cAge > 60)
        known.Queued = true;
    return cAge > 120 ? nullptr : &*known.State;
}

int32_t FakeEngine::StealthEye()
{
    uint32_t seeing = 0;
    return HighestDetection(kPlayer, seeing);
}

int32_t FakeEngine::HighestDetection(uint32_t aTarget, uint32_t& aSeeing)
{
    aSeeing = 0;
    if (aTarget == kPlayer && HeldEye)
    {
        aSeeing = HeldEye->Seeing;
        return HeldEye->Level;
    }
    // The player's: its flagged witnesses skipped (41408); another actor's: as StealthEye::Read, every exclusion.
    const auto cCounts = [&](const Actor& acWitness) {
        if (aTarget == kPlayer)
            return !acWitness.NotOnStealthMeter;
        return DetectionRules::CountsOnEye(acWitness.NotOnStealthMeter, acWitness.Teammate, acWitness.Commander == aTarget, acWitness.Copy);
    };
    int32_t highest = DetectionRules::kUnseen;
    for (const auto& [cId, cActor] : Actors)
        if (cId != aTarget && cActor.Loaded && !cActor.Dead && cCounts(cActor))
            if (const auto* cpState = FreshState(cId, aTarget))
            {
                highest = std::max(highest, cpState->Level);
                aSeeing += cpState->Seen;
            }
    return highest;
}

float FakeEngine::LowestStealthPoints(uint32_t aTarget) const
{
    if (aTarget == kPlayer && HeldEye)
        return HeldEye->Stealth;
    const auto it = StealthPoints.find(aTarget);
    return it != StealthPoints.end() ? it->second : 100.f;
}

int32_t FakeEngine::EyeShows()
{
    uint32_t seeing = 0;
    const int32_t cLevel = HighestDetection(kPlayer, seeing);
    return DetectionRules::EyeShows(PlayerInCombat, cLevel, LowestStealthPoints(kPlayer));
}

void FakeEngine::ProcessTracking()
{
    for (auto& [cId, actor] : Actors)
    {
        if (cId == kPlayer || !actor.Loaded || actor.Dead || actor.KnockedDown)
            continue;
        auto& slots = actor.Ai.HeadTrackIds;
        const auto cTargets = HeadTrackRead::Targets(slots, cId);
        if (cTargets.empty() || cTargets[0] != kPlayer || !slots[0] || !DetectionRules::HeadTrackAsksDetection(actor.Copy))
            continue;
        if (const auto* cpState = FreshState(cId); cpState && cpState->Level <= 0)
        {
            slots[0] = 0;
            if (!actor.Copy)
                World::Get().GetRunner().Trigger(AiStateChangeEvent{cId});
        }
    }
}

void FakeEngine::DetectionPass()
{
    for (auto& [cTarget, known] : DetectionOf)
        for (auto& [cWitness, state] : known)
        {
            if (!state.Queued)
                continue;
            const auto& cSees = SeesOf[cTarget];
            const auto cComputed = cSees.find(cWitness);
            DetectionRules::Pass computed{};
            if (cComputed != cSees.end())
                computed = {.Level = cComputed->second, .LineOfSight = true, .Seen = true};
            const bool cCopy = Actors.contains(cWitness) && Actors.at(cWitness).Copy;
            state = {DetectionRules::Installed(cCopy, cTarget == kPlayer, computed), DetectionFrame, false};
        }
}

HitRules::Party FakeEngine::PartyOf(uint32_t aFormId) const noexcept
{
    using enum HitRules::Party;
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return None;
    if (aFormId == kPlayer)
        return LocalPlayer;
    if (it->second.Copy)
        return it->second.Player ? OtherPlayer : Copy;
    return Local;
}

// HookDamageActor: reads the hit, HitRules::Lands decides.
void FakeEngine::TakesDamage(uint32_t aHittee, uint32_t aHitter, float aDamage, bool aApplyingEffect, bool aWorldHit, bool aCancelledBlow)
{
    const auto cVerdict = HitRules::Lands(PartyOf(aHittee), PartyOf(aHitter), aCancelledBlow, aApplyingEffect, aWorldHit, aWorldHit && HoldSession != 0);
    if (cVerdict.Hit)
        World::Get().GetRunner().Trigger(HitEvent(aHitter, aHittee));
    switch (cVerdict.To)
    {
    case HitRules::Route::Apply:
        World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage));
        m_applyingOwnHit = true;
        DamageActorHealth(aHittee, aHitter, aDamage);
        m_applyingOwnHit = false;
        break;
    case HitRules::Route::AskOwner: World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage, aHitter)); break;
    case HitRules::Route::Mirror:
    case HitRules::Route::Drop: break;
    }
}

void FakeEngine::TouchesTrap(uint32_t aActor, uint32_t aTrap)
{
    World& world = World::Get();
    switch (TrapRules::OfContact(world.GetTransport().IsOnline(), world.IsHosting(), PartyOf(aActor), false))
    {
    case TrapRules::Contact::Raise: RunsTrapScript(aTrap, aActor); break;
    case TrapRules::Contact::AskHost: world.GetRunner().Trigger(TrapContactEvent{aTrap, aActor, {.Velocity = {1.f, 2.f, 3.f}}}); break;
    case TrapRules::Contact::Drop: break;
    }
}

void FakeEngine::RunsTrapScript(uint32_t aTrap, uint32_t aActor)
{
    if (!DecidesWorld) // a guest: WorldEvents drops a trap's events
        return;
    auto& trap = References.at(aTrap);
    trap.TrapContacts.push_back(aActor);
    if (!trap.TrapCanHit)
        return;
    World& world = World::Get();
    switch (TrapRules::OfHit(world.GetTransport().IsOnline(), world.IsHosting(), PartyOf(aActor)))
    {
    case TrapRules::Hit::Apply: TakesDamage(aActor, 0, trap.TrapDamage, false); break;
    case TrapRules::Hit::AskOwner: world.GetRunner().Trigger(TrapHitEvent{aTrap, aActor, {.Damage = trap.TrapDamage}}); break;
    case TrapRules::Hit::Drop: break;
    }
}
