#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>

#include <Structs/ActionEvent.h>

#include <Games/Animation/ActorMediator.h>
#include <Games/Animation/TESActionData.h>

#include <Misc/BSFixedString.h>
#include <Misc/ActorState.h>

#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BSSynchronizedClipGenerator.h>

#include <World.h>
#include <Systems/AnimationSystem.h>

using TPerformAction = uint8_t(ActorMediator*, TESActionData* apAction);
static TPerformAction* RealPerformAction;
using TUpdateAnimationGraphs = bool(TESObjectREFR*, float aDelta);
static TUpdateAnimationGraphs* RealUpdateAnimationGraphs;
using TUpdateSynchronizedClip = void(BSSynchronizedClipGenerator*, const hkbContext& acContext, float aTimestep);
static TUpdateSynchronizedClip* RealUpdateSynchronizedClip;

// TODO: make scoped override
thread_local bool g_forceAnimation = false;
// The local player's own action under way: a paired one (a kill move) starts its partner's half inside it.
thread_local bool s_playerAction = false;

uint8_t HookPerformAction(ActorMediator* apThis, TESActionData* apAction)
{
    auto pActor = apAction->actor;
    const auto pExtension = pActor->GetExtension();
    // A copy's half of the local player's paired action: taken as the player's game plays it, never sent
    // (its owner's game plays it from the player's action).
    const bool cPlayersPartner = s_playerAction && pExtension->IsCopy() && apAction->target == PlayerCharacter::Get();

    if (!pExtension->IsCopy() || g_forceAnimation || cPlayersPartner)
    {
        ActionEvent action;
        action.State1 = pActor->actorState.flags1;
        action.State2 = pActor->actorState.flags2;
        action.Type = apAction->unkInput | (apAction->someFlag ? 0x4 : 0);
        action.Tick = World::Get().GetTick();
        action.ActorId = pActor->formID;
        action.ActionId = apAction->action->formID;
        action.TargetId = apAction->target ? apAction->target->formID : 0;

        pActor->SaveAnimationVariables(action.Variables);

        const bool cOuterPlayerAction = pActor == PlayerCharacter::Get() && !s_playerAction;
        s_playerAction |= cOuterPlayerAction;
        const auto res = RealPerformAction(apThis, apAction);
        if (cOuterPlayerAction)
            s_playerAction = false;

        // spdlog::debug("Action event name: {}, target name: {}", apAction->eventName.AsAscii(), apAction->targetEventName.AsAscii());

        // This is a weird case where it gets spammed and doesn't do much, not sure if it still needs to be sent over the network
        if (apAction->someFlag == 1)
            return res;

        action.EventName = apAction->eventName.AsAscii();
        action.TargetEventName = apAction->targetEventName.AsAscii();
        action.IdleId = apAction->idleForm ? apAction->idleForm->formID : 0;

        // An action the engine refused changed nothing here: replayed on a copy it could take
        // (e.g. a late 'MRh_Equipped_Event' after the hands were lowered) and leave the copy
        // showing something its owner never did.
        if (!res)
        {
            if (!g_forceAnimation && !cPlayersPartner)
                spdlog::debug("Owner {:X} action {:X} '{}' refused by the engine, not sent", pActor->formID, action.ActionId, action.EventName);
            return res;
        }

        // What this actor's graph last took, owner or copy (see AnimationSystem for replays).
        pExtension->LatestAnimation = action;
        if (apAction->action->formID == 0x132AF)
            pExtension->LatestWeapEquipAnimation = action;

        // Forced by us (a copy converging, a special case of ours) or a copy's paired half: never the owner's own action.
        if (g_forceAnimation || cPlayersPartner)
            return res;

        if (ActorState::LifeStateOf(action.State1) == ActorState::kLifeDying)
            spdlog::debug("Owner {:X} death action {:X} '{}'", pActor->formID, action.ActionId, action.EventName);
        World::Get().GetRunner().Trigger(action);

        return res;
    }

    return 0;
}

// TESObjectREFR::sub_20123 (UpdateAnimationGraphs, our name): an actor's graph update in its animation job
// (RunOneActorAnimationUpdateJob 41453). A copy's replayed action is applied in step with it.
bool HookUpdateAnimationGraphs(TESObjectREFR* apThis, float aDelta)
{
    // Runs for every animated reference each frame: a form type test, not an RTTI cast (copies are never the player).
    if (apThis->formType == Actor::Type && apThis != PlayerCharacter::Get())
    {
        Actor* pActor = static_cast<Actor*>(apThis);
        if (pActor->GetExtension()->IsCopy())
            AnimationSystem::RunReplayStep(pActor);
    }
    return RealUpdateAnimationGraphs(apThis, aDelta);
}

// A copy's half of a paired animation is at its mark: its owner's game placed it, and the engine cannot walk
// a copy there, so a kill move with a copy waited out the engine's fallback (docs/re/systems/actor-ai-and-copies.md).
void HookUpdateSynchronizedClip(BSSynchronizedClipGenerator* apThis, const hkbContext& acContext, float aTimestep)
{
    RealUpdateSynchronizedClip(apThis, acContext, aTimestep);
    if (apThis->atMark || !acContext.character)
        return;
    const auto* pGraph = reinterpret_cast<const BShkbAnimationGraph*>(reinterpret_cast<const uint8_t*>(acContext.character) -
                                                                      offsetof(BShkbAnimationGraph, character));
    if (pGraph->holder && pGraph->holder->GetExtension()->IsCopy())
        apThis->atMark = true;
}

ActorMediator* ActorMediator::Get() noexcept
{
    POINTER_SKYRIMSE(ActorMediator*, s_actorMediator, 403567);

    return *(s_actorMediator.Get());
}

bool ActorMediator::PerformAction(TESActionData* apAction) noexcept
{
    if (apAction->actor->formID == 0x13482)
    {
        /*static std::unordered_set<uint32_t> s_ids;

        spdlog::error("New frame");
        for(auto i = 0; i < action.Variables.size(); ++i)
        {
            auto& oldVars = pExtension->LatestVariables.Variables;
            auto& newVars = action.Variables;
            if(oldVars[i] != newVars[i] && s_ids.count(i) == 0)
            {
                //s_ids.insert(i);
                spdlog::info("Var {} changed from {} to {}", i, oldVars[i], newVars[i]);
            }
        }*/
        // spdlog::info("Play animation name: {} with idle {:X} and target {:X} and unk {:X}", apAction->action->keyword.AsAscii(), (apAction->idleForm ? apAction->idleForm->formID : 0), (apAction->target ? apAction->target->formID : 0), apAction->unkInput);
    }

    const auto res = RealPerformAction(this, apAction);
    // const auto res = RePerformAction(apAction, aValue);

    if (res && apAction->actor->formID == 0x13482)
    {
        //    spdlog::info("Passed !");
    }

    return res != 0;
}

bool ActorMediator::ForceAction(TESActionData* apAction) noexcept
{
    using TAnimationStep = uint8_t(ActorMediator*, TESActionData*);
    using TApplyAnimationVariables = void*(void*, TESActionData*);

    POINTER_SKYRIMSE(TApplyAnimationVariables, ApplyAnimationVariables, 39004);
    POINTER_SKYRIMSE(TAnimationStep, PerformComplexAction, 38953);
    POINTER_SKYRIMSE(void*, qword_142F271B8, 403566);

    uint8_t result = 0;

    auto pActor = static_cast<Actor*>(apAction->actor);
    if (!pActor || EngineVirtual(pActor->animationGraphHolder).IsReady())
    {
        result = PerformComplexAction(this, apAction);

        ApplyAnimationVariables(*qword_142F271B8.Get(), apAction);
    }

    return result;
}

ActionInput::ActionInput(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
{
    // skip vtable as we never use this directly
    actor = apActor;
    target = apTarget;
    action = apAction;
    unkInput = aParam1;
}

void ActionInput::Release()
{
    actor.Release();
    target.Release();
}

ActionOutput::ActionOutput()
    : eventName("")
    , targetEventName("")
{
    // skip vtable as we never use this directly

    result = 0;
    targetIdleForm = nullptr;
    idleForm = nullptr;
    unk1 = 0;
}

void ActionOutput::Release()
{
    eventName.Release();
    targetEventName.Release();
}

BGSActionData::BGSActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : ActionInput(aParam1, apActor, apAction, apTarget)
{
    // skip vtable as we never use this directly
    someFlag = 0;
}

TESActionData::TESActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : BGSActionData(aParam1, apActor, apAction, apTarget)
{
    POINTER_SKYRIMSE(void*, s_vtbl, 188603);

    someFlag = false;

    *reinterpret_cast<void**>(this) = s_vtbl.Get();
}

TESActionData::~TESActionData()
{
    ActionOutput::Release();
    ActionInput::Release();
}

static Hooks::Initializer s_animationHook(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TPerformAction, performAction, 38949);

        POINTER_SKYRIMSE(TUpdateAnimationGraphs, updateAnimationGraphs, 20123);
        POINTER_SKYRIMSE(TUpdateSynchronizedClip, updateSynchronizedClip, 63050); // BSSynchronizedClipGenerator::Update (vtable 5)

        RealPerformAction = performAction.Get();
        RealUpdateAnimationGraphs = updateAnimationGraphs.Get();

        HOOK(&RealPerformAction, HookPerformAction);
        RealUpdateSynchronizedClip = updateSynchronizedClip.Get();
        HOOK(&RealUpdateAnimationGraphs, HookUpdateAnimationGraphs);
        HOOK(&RealUpdateSynchronizedClip, HookUpdateSynchronizedClip);
    });
