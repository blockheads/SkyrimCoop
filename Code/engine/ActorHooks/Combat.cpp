// Actor's combat hooks: damage, effects' damage, regeneration, hostility.
#include <CopyCombatGroups.h>
#include <Forms/BGSAction.h>
#include <Interface/ItemMenuWatch.h>
#include <animation/ActorMediator.h>
#include <Animation/TESActionData.h>
#include <animation/ReplayState.h>
#include <animation/ActorStateRules.h>
#include <animation/GraphReference.h>
#include <CrashHandler.h>
#include <HookSwitch.h>
#include <BSCore/LockOrder.h>
#include <core/EngineNames.h>
#include <References.h>
#include <characters/ActorPools.h>
#include <EquipManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Havok/bhkCharacterController.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <movement/CopyStep.h>
#include <Combat/WorldHits.h>
#include <Misc/GameVM.h>
#include <DefaultObjectManager.h>
#include <Forms/TESNPC.h>
#include <Forms/TESFaction.h>
#include <Components/TESActorBaseData.h>
#include <ExtraData/ExtraFactionChanges.h>
#include <Memory.h>
#include <RefrInteraction.h>
#include <Forms/TESLevItem.h>
#include <Combat/CombatController.h>

#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <Papyrus/GuestScripts.h>
#include <items/InventoryChangeEvent.h>
#include <items/WornResetEvent.h>
#include <items/LooseItemEvents.h>
#include <mounts/MountEvent.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <dialogue/DialogueEvent.h>
#include <combat/HitEvent.h>
#include <Combat/HitOwnership.h>
#include <Combat/PartyAggro.h>
#include <magic/RemoveSpellEvent.h>

#include <TES.h>
#include <GuestNpcs.h>
#include <characters/NpcHeldEvent.h>
#include <core/World.h>
#include <ports/IActorBridge.h>
#include <world/ActivatePrompts.h>
#include <dialogue/SpeakerUpdate.h>
#include <Bridges/PapyrusService.h>
#include <death/DeathDrops.h>

#include <Forms/ActorValueInfo.h>
#include <Forms/TESRace.h>

#include <Effects/ValueModifierEffect.h>

#include <Misc/InventoryEntry.h>
#include <ExtraData/ExtraCount.h>
#include <Misc/ActorKnowledge.h>
#include <Crime.h>
#include <crime/DetectionRules.h>

#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraInteraction.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>

#include <BSAnimationGraphManager.h>

#include <Hooks/Initializer.h>

using TDamageActor = bool(Actor*, float aDamage, Actor* apHitter, bool aKillMove);

static TDamageActor* RealDamageActor = nullptr;

// TODO: this is flawed, since it does not account for invulnerable actors
// Set while an active effect applies its value (HookApplyActorEffect): damage then comes from an
// effect this game owns, never from a replayed melee swing.
static thread_local bool s_applyingEffect = false;

bool HookDamageActor(Actor* apThis, float aDamage, Actor* apHitter, bool aKillMove)
{
    // Reads the hit; HitRules::Lands decides (combat/HitRules.h, docs/design/friendly-fire.md).
    const bool cCancelled = apHitter && ReplayState::CancelledBlow(World::FrameTick() < apHitter->GetExtension()->ClashUntil);
    const bool cWorldHit = WorldHits::Current();
    const auto cVerdict = HitRules::Lands(HitRules::PartyOf(apThis), HitRules::PartyOf(apHitter), cCancelled, s_applyingEffect, cWorldHit,
                                          cWorldHit && GuestNpcs::Session() != 0);
    if (cVerdict.Hit)
        World::Runner().Trigger(HitEvent(apHitter->formID, apThis->formID));
    if (cCancelled)
    {
        spdlog::debug("{:X}'s blow on {:X} dropped: its pair yielded", apHitter->formID, apThis->formID);
        return false;
    }

    const float cRealDamage = GameplayFormulas::CalculateRealDamage(apThis, aDamage, aKillMove);
    const bool cWouldKill = (apThis->GetActorValue(ActorValueInfo::kHealth) - cRealDamage) <= 0.f;
    switch (cVerdict.To)
    {
    case HitRules::Route::Apply:
        World::Runner().Trigger(HealthChangeEvent(apThis->formID, -cRealDamage));
        return RealDamageActor(apThis, aDamage, apHitter, aKillMove);
    case HitRules::Route::AskOwner:
        World::Runner().Trigger(HealthChangeEvent(apThis->formID, -cRealDamage, apHitter ? apHitter->formID : 0));
        return cWouldKill;
    case HitRules::Route::Mirror: return cWouldKill;
    case HitRules::Route::Drop: return false;
    }
    return false;
}

// DealDamage 38627 (Precision's name; void: its early returns leave RAX unset): one weapon hit on its chosen target.
// Dropped between players on one horse (HitRules::RideTogether, docs/re/systems/player-mount.md).
using TDealDamage = void(Actor* apAttacker, Actor* apTarget, void* apArrow, bool aLeftHand);

static TDealDamage* RealDealDamage = nullptr;

void HookDealDamage(Actor* apAttacker, Actor* apTarget, void* apArrow, bool aLeftHand)
{
    if (HitRules::RideTogether(apTarget, apAttacker))
        return;
    RealDealDamage(apAttacker, apTarget, apArrow, aLeftHand);
}

using TApplyActorEffect = void(ActiveEffect*, Actor* apTarget, float aEffectValue, unsigned int unk1);

static TApplyActorEffect* RealApplyActorEffect = nullptr;

void HookApplyActorEffect(ActiveEffect* apThis, Actor* apTarget, float aEffectValue, unsigned int unk1)
{
    struct ScopedEffect
    {
        ScopedEffect() { s_applyingEffect = true; }
        ~ScopedEffect() { s_applyingEffect = false; }
    } scope;

    const auto* pValueModEffect = Cast<ValueModifierEffect>(apThis);

    if (pValueModEffect)
    {
        if (pValueModEffect->actorValueIndex == ActorValueInfo::kHealth && aEffectValue > 0.0f)
        {
            if (apTarget && apTarget->GetExtension())
            {
                const auto pExTarget = apTarget->GetExtension();
                if (pExTarget->IsLocal())
                {
                    World::Runner().Trigger(HealthChangeEvent(apTarget->formID, aEffectValue));
                    return RealApplyActorEffect(apThis, apTarget, aEffectValue, unk1);
                }
                return;
            }
        }
    }

    return RealApplyActorEffect(apThis, apTarget, aEffectValue, unk1);
}

// The passive regeneration of health (24), magicka (25) and stamina (26): aDelta seconds' worth, restored
// into the damage modifier. The per-value updates (38451 health, 38452 stamina, 38453 magicka) call it every
// frame. Copies take these values from their owner, so they never regenerate. (Upstream hooked 37448,
// Actor::GetSubmergedLevel, here since its 2021 AE port; see docs/re/systems/actor-values.md.)
using TRegenerateActorValue = void(Actor*, int32_t aActorValue, float aDelta);

static TRegenerateActorValue* RealRegenerateActorValue = nullptr;

void HookRegenerateActorValue(Actor* apThis, int32_t aActorValue, float aDelta)
{
    if (apThis->GetExtension()->IsCopy())
        return;

    RealRegenerateActorValue(apThis, aActorValue, aDelta);
}

using TIsFleeing = bool(Actor*);

static TIsFleeing* RealIsFleeing = nullptr;

bool HookIsFleeing(Actor* apThis)
{
    // TODO: Player or RemotePlayer? Can players be in fleeing mode in skyrim?
    // TODO: investigate why the flee flag is set at all on remote players sometimes.
    if (apThis->GetExtension()->IsPlayer())
        return false;
    
    return RealIsFleeing(apThis);
}

using TIsHostileToActor = bool(Actor*, Actor* apOther);

static TIsHostileToActor* RealIsHostileToActor = nullptr;

// Hang diagnostics: every loaded actor's reference lock (TESObjectREFR+0x88, BSReadWriteLock
// {writer thread id, lock word}); held ones are logged from the watchdog thread (read-only peeks
// of pointers refreshed by the main thread).
static std::atomic<Actor*> s_lockWatch[128]{};

static void LogHeldReferenceLocks()
{
    for (auto& slot : s_lockWatch)
        if (Actor* pActor = slot.load())
        {
            const auto* pLock = reinterpret_cast<const volatile uint32_t*>(reinterpret_cast<const uint8_t*>(pActor) + 0x88);
            if (pLock[0] || pLock[1])
                spdlog::error("HangWatchdog: actor {:X} reference lock held: writer thread {}, lock word {:#x}", pActor->formID, uint32_t(pLock[0]), uint32_t(pLock[1]));
        }
    spdlog::error("HangWatchdog: (end of held reference locks)");
    LockOrder::LogHeldLocks();
}

static bool DescribeReferenceLock(const void* apLock, char* apBuffer, size_t aSize)
{
    for (auto& slot : s_lockWatch)
        if (Actor* pActor = slot.load())
            if (reinterpret_cast<const uint8_t*>(pActor) + 0x88 == apLock)
            {
                *std::format_to_n(apBuffer, aSize - 1, "actor {:X} reference lock", pActor->formID).out = '\0';
                return true;
            }
    return false;
}

void RefreshHangLockWatch() noexcept
{
    [[maybe_unused]] static const bool s_diagnosticSet = [] {
        CrashHandler::SetHangDiagnostic(&LogHeldReferenceLocks);
        // Mutex::Lock1 (13129), the engine's BSSpinLock wait: the lock stays in rdi while it sleeps.
        POINTER_SKYRIMSE(void, s_spinLockWait, 13129);
        CrashHandler::SetSpinLockWait(reinterpret_cast<DWORD64>(s_spinLockWait.Get()), [](const CONTEXT& acFrame) { return acFrame.Rdi; });
        LockOrder::SetLockDescriber(&DescribeReferenceLock);
        return true;
    }();
    size_t index = 0;
    if (auto* pPlayer = Cast<Actor>(TESForm::GetById(0x14)))
        s_lockWatch[index++] = pPlayer;
    for (const uint32_t cFormId : World::Get().GetActorBridge().GetLoadedActorFormIds())
        if (index < std::size(s_lockWatch))
            if (auto* pActor = Cast<Actor>(TESForm::GetById(cFormId)))
                s_lockWatch[index++] = pActor;
    for (; index < std::size(s_lockWatch); ++index)
        s_lockWatch[index] = nullptr;
}

bool HookIsHostileToActor(Actor* apThis, Actor* apOther)
{
    if (PartyAggroNative::IsPartyEnemy(apThis, apOther))
        return true;
    return RealIsHostileToActor(apThis, apOther);
}

// The reaction (0 neutral, 1 enemy, 2 ally, 3 friend), cached (37666) or computed (37667): every player is the enemy
// of an actor angry with one or fighting one (combat-targets.md, A fight with one player is the party's).
constexpr int32_t cReactionEnemy = 1;

using TGetFactionReaction = int32_t(Actor*, Actor* apOther);

static TGetFactionReaction* RealGetFactionReaction = nullptr;

int32_t HookGetFactionReaction(Actor* apThis, Actor* apOther)
{
    if (PartyAggroNative::IsPartyEnemy(apThis, apOther))
        return cReactionEnemy;
    return RealGetFactionReaction(apThis, apOther);
}

// The core "may this actor attack that one" check (CanAttackActor, IsHostileToActor and combat
// use it). It reads the relationship cached in the actor's knowledge of the other directly, which
// still says ally right after the actor got angry, so answer it here too.
using TShouldAttack = bool(Actor*, Actor* apOther, int32_t aAggression, uint8_t aFlags);

static TShouldAttack* RealShouldAttack = nullptr;

bool HookShouldAttack(Actor* apThis, Actor* apOther, int32_t aAggression, uint8_t aFlags)
{
    // Inside CopyCombatGroups::Hold the copy's owner's fight already answered it; a copy has no knowledge of its own.
    if (PartyAggroNative::IsPartyEnemy(apThis, apOther) || CopyCombatGroups::Holding())
        return true;
    return RealShouldAttack(apThis, apOther, aAggression, aFlags);
}

using TComputeReaction = int32_t(Actor*, Actor* apOther, bool aUnk);

static TComputeReaction* RealComputeReaction = nullptr;

int32_t HookComputeReaction(Actor* apThis, Actor* apOther, bool aUnk)
{
    if (PartyAggroNative::IsPartyEnemy(apThis, apOther))
        return cReactionEnemy;
    return RealComputeReaction(apThis, apOther, aUnk);
}

static Hooks::Initializer s_actorCombatHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(37335, &RealDamageActor, HookDamageActor);
        HOOK_ID(38627, &RealDealDamage, HookDealDamage);
        HOOK_ID(35086, &RealApplyActorEffect, HookApplyActorEffect);
        HOOK_ID(38459, &RealRegenerateActorValue, HookRegenerateActorValue);
        HOOK_ID(37537, &RealIsHostileToActor, HookIsHostileToActor);
        HOOK_ID(37666, &RealGetFactionReaction, HookGetFactionReaction);
        HOOK_ID(37534, &RealShouldAttack, HookShouldAttack);
        HOOK_ID(37667, &RealComputeReaction, HookComputeReaction);
        HOOK_ID(37577, &RealIsFleeing, HookIsFleeing);
    });
