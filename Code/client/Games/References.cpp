#include <HookSwitch.h>

#include <Games/References.h>

#include <RTTI.h>

#include <Forms/BGSHeadPart.h>
#include <Forms/TESNPC.h>
#include <Forms/TESPackage.h>
#include <Forms/TESWeather.h>
#include <SaveLoad.h>
#include <ExtraData/ExtraDataList.h>

#include <Misc/GameVM.h>

#include <Games/TES.h>
#include <Games/Overrides.h>
#include <Games/Misc/Lock.h>
#include <AI/AIProcess.h>
#include <Magic/MagicCaster.h>
#include <Sky/Sky.h>


#include <Wire/Serialization.h>

#include <Services/PapyrusService.h>
#include <Services/DebugService.h>
#include <World.h>

#include <AI/AITimer.h>
#include <Combat/CombatTargetSelector.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace Settings
{
int32_t* GetDifficulty() noexcept
{
    POINTER_SKYRIMSE(int32_t, s_difficulty, 381472);
    return s_difficulty.Get();
}

float* GetGreetDistance() noexcept
{
    POINTER_SKYRIMSE(float, s_greetDistance, 370892);
    return s_greetDistance.Get();
}

} // namespace Settings

namespace GameplayFormulas
{

float CalculateRealDamage(Actor* apHittee, float aDamage, bool aKillMove) noexcept
{
    using TGetDifficultyMultiplier = float(int32_t, int32_t, bool);
    POINTER_SKYRIMSE(TGetDifficultyMultiplier, s_getDifficultyMultiplier, 26503);

    bool isPlayer = apHittee == PlayerCharacter::Get();

    float multiplier = s_getDifficultyMultiplier(PlayerCharacter::Get()->difficulty, ActorValueInfo::kHealth, isPlayer);

    float realDamage = aDamage;

    // TODO(cosideci): this seems problematic? It may not register the kill for others?
    // Disabled for now, cause this check seems to have totally broken everything, let's see what happens.
    // if (!aKillMove || multiplier < 1.0)
    realDamage = aDamage * multiplier;

    return realDamage;
}

} // namespace GameplayFormulas

void FadeOutGame(bool aFadingOut, bool aBlackFade, float aFadeDuration, bool aRemainVisible, float aSecondsToFade) noexcept
{
    using TFadeOutGame = void(bool, bool, float, bool, float);
    POINTER_SKYRIMSE(TFadeOutGame, fadeOutGame, 52847);
    fadeOutGame.Get()(aFadingOut, aBlackFade, aFadeDuration, aRemainVisible, aSecondsToFade);
}

using TCheckForNewPackage = bool(void*, Actor* apActor, uint64_t aUnk1);
static TCheckForNewPackage* RealCheckForNewPackage = nullptr;

// A copy picks no packages (copy contract): it holds its owner's (SetCopyAiFields), else the engine's do-nothing one,
// so the engine treats it like any NPC with a package; HookUpdateAI keeps it from running.
bool HookCheckForNewPackage(void* apThis, Actor* apActor, uint64_t aUnk1)
{
    if (apActor && apActor->GetExtension()->IsCopy())
    {
        const AIProcess* pProcess = apActor->currentProcess;
        const TESPackage* pPackage = pProcess ? pProcess->currentPackage.package : nullptr;
        const uint32_t cOwnerPackageId = apActor->GetExtension()->OwnerPackageId;
        auto* pOwnerPackage = cOwnerPackageId ? Cast<TESPackage>(TESForm::GetById(cOwnerPackageId)) : nullptr;
        if (pProcess && pOwnerPackage)
        {
            if (pPackage != pOwnerPackage)
                apActor->PutOwnerPackage(pOwnerPackage);
        }
        else if (pProcess && !(pPackage && pPackage->packType == TESPackage::kDoNothing))
            apActor->InitiateDoNothingPackage();
        return false;
    }

    return RealCheckForNewPackage(apThis, apActor, aUnk1);
}

// Packages the engine creates on the fly (movement blocked, flee, ...) all arrive here.
using TPutCreatedPackage = void(Actor*, TESPackage* apPackage, bool aRunOnce, bool aUnk);
static TPutCreatedPackage* RealPutCreatedPackage = nullptr;

// Set while Actor::InitiateDoNothingPackage gives a copy its package.
static thread_local bool s_copyPackage = false;

void HookPutCreatedPackage(Actor* apThis, TESPackage* apPackage, bool aRunOnce, bool aUnk)
{
    if (apThis->GetExtension()->IsCopy() && !s_copyPackage)
        return;

    RealPutCreatedPackage(apThis, apPackage, aRunOnce, aUnk);
}

// Actor::GetCurrentPackage (37399): what GetIsCurrentPackage (21529) and Papyrus read.
TESPackage* Actor::GetCurrentPackage() const noexcept
{
    using TGetCurrentPackage = TESPackage*(const Actor*);
    POINTER_SKYRIMSE(TGetCurrentPackage, s_getCurrentPackage, 37399);
    return s_getCurrentPackage(this);
}

// ActorPackage::InitFromPackage (38959): ends the old package's data, makes the new one's; runs nothing.
void Actor::PutOwnerPackage(TESPackage* apPackage) noexcept
{
    using TInitFromPackage = void(ActorPackage*, TESPackage*, TESObjectREFR* apTarget, Actor*);
    POINTER_SKYRIMSE(TInitFromPackage, s_initFromPackage, 38959);
    if (currentProcess && apPackage)
        s_initFromPackage(&currentProcess->currentPackage, apPackage, nullptr, this);
}

// Actor::InitiateDoNothingPackage (37402): only in high or middle-high process.
void Actor::InitiateDoNothingPackage() noexcept
{
    using TInitiateDoNothingPackage = void(Actor*);
    POINTER_SKYRIMSE(TInitiateDoNothingPackage, s_initiateDoNothingPackage, 37402);

    s_copyPackage = true;
    s_initiateDoNothingPackage(this);
    s_copyPackage = false;
}

using TSetCurrentPickREFR = void(Console*, BSPointerHandle<TESObjectREFR>* apRefr);
static TSetCurrentPickREFR* RealSetCurrentPickREFR = nullptr;

void HookSetCurrentPickREFR(Console* apThis, BSPointerHandle<TESObjectREFR>* apRefr)
{
    uint32_t formId = 0;

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(apRefr->handle.iBits);
    if (pObject)
        formId = pObject->formID;

    World::Get().GetDebugService().SetDebugId(formId);

    return RealSetCurrentPickREFR(apThis, apRefr);
}

static Hooks::Initializer s_referencesHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(39114, &RealCheckForNewPackage, HookCheckForNewPackage);
        HOOK_ID(51093, &RealSetCurrentPickREFR, HookSetCurrentPickREFR);
        HOOK_ID(37570, &RealPutCreatedPackage, HookPutCreatedPackage);
    });
