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

#include <World.h>
#include <Systems/AnimationSystem.h>

TP_THIS_FUNCTION(TPerformAction, uint8_t, ActorMediator, TESActionData* apAction);
static TPerformAction* RealPerformAction;
TP_THIS_FUNCTION(TUpdateAnimationGraphs, bool, TESObjectREFR, float aDelta);
static TUpdateAnimationGraphs* RealUpdateAnimationGraphs;

// TODO: make scoped override
thread_local bool g_forceAnimation = false;

uint8_t TP_MAKE_THISCALL(HookPerformAction, ActorMediator, TESActionData* apAction)
{
    auto pActor = apAction->actor;
    const auto pExtension = pActor->GetExtension();

    if (!pExtension->IsRemote() || g_forceAnimation)
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

        const auto res = TiltedPhoques::ThisCall(RealPerformAction, apThis, apAction);

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
            if (!g_forceAnimation)
                spdlog::debug("Owner {:X} action {:X} '{}' refused by the engine, not sent", pActor->formID, action.ActionId, action.EventName);
            return res;
        }

        // What this actor's graph last took, owner or copy (see AnimationSystem for replays).
        pExtension->LatestAnimation = action;
        if (apAction->action->formID == 0x132AF)
            pExtension->LatestWeapEquipAnimation = action;

        // Forced by us (a copy converging, a special case of ours): never the owner's own action.
        if (g_forceAnimation)
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
bool TP_MAKE_THISCALL(HookUpdateAnimationGraphs, TESObjectREFR, float aDelta)
{
    // Runs for every animated reference each frame: a form type test, not an RTTI cast (copies are never the player).
    if (apThis->formType == Actor::Type && apThis != PlayerCharacter::Get())
    {
        Actor* pActor = static_cast<Actor*>(apThis);
        if (pActor->GetExtension()->IsRemote())
            AnimationSystem::RunReplayStep(pActor);
    }
    return TiltedPhoques::ThisCall(RealUpdateAnimationGraphs, apThis, aDelta);
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
        /*static Set<uint32_t> s_ids;

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

    const auto res = TiltedPhoques::ThisCall(RealPerformAction, this, apAction);
    // const auto res = RePerformAction(apAction, aValue);

    if (res && apAction->actor->formID == 0x13482)
    {
        //    spdlog::info("Passed !");
    }

    return res != 0;
}

bool ActorMediator::ForceAction(TESActionData* apAction) noexcept
{
    TP_THIS_FUNCTION(TAnimationStep, uint8_t, ActorMediator, TESActionData*);
    using TApplyAnimationVariables = void*(void*, TESActionData*);

    POINTER_SKYRIMSE(TApplyAnimationVariables, ApplyAnimationVariables, 39004);
    POINTER_SKYRIMSE(TAnimationStep, PerformComplexAction, 38953);
    POINTER_SKYRIMSE(void*, qword_142F271B8, 403566);

    uint8_t result = 0;

    auto pActor = static_cast<Actor*>(apAction->actor);
    if (!pActor || EngineVirtual(pActor->animationGraphHolder).IsReady())
    {
        result = TiltedPhoques::ThisCall(PerformComplexAction, this, apAction);

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

static TiltedPhoques::Initializer s_animationHook(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TPerformAction, performAction, 38949);

        POINTER_SKYRIMSE(TUpdateAnimationGraphs, updateAnimationGraphs, 20123);

        RealPerformAction = performAction.Get();
        RealUpdateAnimationGraphs = updateAnimationGraphs.Get();

        TP_HOOK(&RealPerformAction, HookPerformAction);
        TP_HOOK(&RealUpdateAnimationGraphs, HookUpdateAnimationGraphs);
    });
