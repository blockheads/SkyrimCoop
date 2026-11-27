#include "Misc/SubtitleManager.h"
#include <HookSwitch.h>

#include <dialogue/SubtitleEvent.h>

#include <TESObjectREFR.h>
#include <Actor.h>
#include <ActorExtension.h>

#include <Forms/TESTopicInfo.h>
#include <Misc/BSFixedString.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <core/World.h>
#include <VersionDb.h>

SubtitleManager* SubtitleManager::Get() noexcept
{
    POINTER_SKYRIMSE(SubtitleManager*, s_singleton, 400443);
    return *s_singleton.Get();
}

using TShowSubtitle = void(SubtitleManager*, TESObjectREFR* apSpeaker, const char* apSubtitleText, bool aForceDisplay);
static TShowSubtitle* RealShowSubtitle = nullptr;

void SubtitleManager::ShowSubtitle(TESObjectREFR* apSpeaker, const char* apSubtitleText, TESTopicInfo* apTopicInfo, bool aForceDisplay) noexcept
{
    RealShowSubtitle(this, apSpeaker, apSubtitleText, aForceDisplay);
}

void HookShowSubtitle(SubtitleManager* apThis, TESObjectREFR* apSpeaker, const char* apSubtitleText, bool aForceDisplay)
{
    // spdlog::debug("Subtitle for actor {:X} (bool {}):\n\t{}", apSpeaker ? apSpeaker->formID : 0, aForceDisplay, apSubtitleText);

    Actor* pActor = Cast<Actor>(apSpeaker);
    if (apSubtitleText && pActor && pActor->GetExtension()->IsLocal() && !pActor->GetExtension()->IsPlayer())
        World::Get().GetRunner().Trigger(SubtitleEvent(apSpeaker->formID, apSubtitleText, aForceDisplay));
    // A copy shows only its owner's subtitles (NotifySubtitle calls past this hook).
    if (pActor && pActor->GetExtension()->IsCopy())
        return;

    RealShowSubtitle(apThis, apSpeaker, apSubtitleText, aForceDisplay);
}

static Hooks::Initializer s_subtitleHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(52626, &RealShowSubtitle, HookShowSubtitle);
    });
