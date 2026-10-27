#include <EffectEvents.h>
#include <Games/HitVisualsRules.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <Effects/ActiveEffect.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Forms/EffectSetting.h>
#include <Magic/EffectItem.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Magic/MagicTarget.h>
#include <VersionDb.h>
#include <World.h>

#include <atomic>

// The rules' bits are the engine's (EffectSetting's flags).
static_assert(HitVisualsRules::kPersist == EffectSetting::kFXPersist && HitVisualsRules::kNoHit == EffectSetting::kNoHitEffect);

namespace EffectEvents
{
namespace
{
std::atomic<uint32_t> s_users{0};

// Each runs where the engine calls it (the actors' job threads too): the change is queued.
void Raise(uint32_t aFormId, uint32_t aFlashEffectId = 0) noexcept
{
    if (aFormId && s_users.load(std::memory_order_relaxed))
        World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aFormId, aFlashEffectId});
}

// A one-shot visual, including an instant fire-and-forget effect with FX Persist.
uint32_t FlashEffectOf(const ActiveEffect* apEffect) noexcept
{
    const auto* pItem = static_cast<const EffectItem*>(apEffect->pEffect);
    const EffectSetting* pSetting = pItem ? pItem->pEffectSetting : nullptr;
    if (!pSetting || !HitVisualsRules::Flash(pSetting->flags, pSetting->pHitShader || pSetting->pHitArt,
                                            apEffect->fDuration, pSetting->castType))
        return 0;
    return pSetting->formID;
}

void Raise(ActiveEffect* apEffect, bool aStarted) noexcept
{
    if (Actor* pActor = apEffect->pTarget ? apEffect->pTarget->GetTargetAsActor() : nullptr)
        Raise(pActor->formID, aStarted ? FlashEffectOf(apEffect) : 0);
}

using TActiveEffectVisuals = void(ActiveEffect*);
TActiveEffectVisuals* RealStartHitEffects = nullptr;
TActiveEffectVisuals* RealStopHitEffects = nullptr;

// 34094 (ours: ActiveEffect::StartHitEffects), from ActiveEffect::Update when the effect starts, again too when its
// conditions hold again: starts the hit visuals, or queues ApplyVisuals (34065) for the main thread.
void HookStartHitEffects(ActiveEffect* apEffect)
{
    RealStartHitEffects(apEffect);
    Raise(apEffect, true);
}

// ActiveEffect::StopHitEffects (34050): the effect ended, went inactive or was dispelled. Only scripted effects send
// TESActiveEffectApplyRemove (ActiveEffect::Update 34092).
void HookStopHitEffects(ActiveEffect* apEffect)
{
    const bool cHadVisuals = apEffect->pHitEffects != nullptr;
    RealStopHitEffects(apEffect);
    if (cHadVisuals)
        Raise(apEffect, false);
}

Hooks::Initializer s_effectHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(34094, &RealStartHitEffects, HookStartHitEffects);
        HOOK_ID(34050, &RealStopHitEffects, HookStopHitEffects);
    });
} // namespace

void Start() noexcept
{
    ++s_users;
}

void Stop() noexcept
{
    if (s_users)
        --s_users;
}
} // namespace EffectEvents
