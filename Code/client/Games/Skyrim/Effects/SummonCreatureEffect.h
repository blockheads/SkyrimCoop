#pragma once

#include "ActiveEffect.h"
#include <HookSwitch.h>

#include <Actor.h>
#include <Games/ActorExtension.h>

struct SummonCreatureEffect : ActiveEffect
{
};

using TInitializeSummonCreatureEffect = void*(Actor*, MagicItem*, EffectItem*);
static TInitializeSummonCreatureEffect* RealInitializeSummonCreatureEffect = nullptr;

void* HookInitializeSummonCreatureEffect(Actor* apThis, MagicItem* apSpell, EffectItem* apEffect)
{
    if (apThis)
    {
        auto pExtension = apThis->GetExtension();
        if (pExtension && pExtension->IsCopy())
            return nullptr;
    }

    return RealInitializeSummonCreatureEffect(apThis, apSpell, apEffect);
}

static Hooks::Initializer s_summonCreatureEffectsHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TInitializeSummonCreatureEffect, s_start, 34989);

        RealInitializeSummonCreatureEffect = s_start.Get();

        HOOK(&RealInitializeSummonCreatureEffect, HookInitializeSummonCreatureEffect);
    });
