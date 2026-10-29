#include <Misc/JudgedFor.h>

#include <HookSwitch.h>

#include <Actor.h>
#include <BSCore/LockOrder.h>
#include <Components/TESCondition.h>
#include <Forms/TESActorBase.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/JudgedRules.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

#include <bit>
#include <optional>

namespace
{
thread_local TESObjectREFR* t_pJudged = nullptr;

using TIsTrue = bool(const TESConditionItem*, ConditionCheckParams&);
TIsTrue* RealIsTrue = nullptr;

// Whom the item runs on, when that is fixed by the item rather than by the params' subject and target.
TESObjectREFR* FixedRunOn(const TESConditionItem& acItem, const ConditionCheckParams& acParams) noexcept
{
    using RunOn = TESConditionItem::RunOn;
    if (acItem.runOn == RunOn::kReference)
        return TESObjectREFR::GetByHandle(acItem.runOnRefHandle);
    if (acItem.runOn == RunOn::kQuestAlias && acParams.quest)
        return acParams.quest->GetAliasedRef(acItem.dataId);
    return nullptr;
}

float SettingOf(Setting& aSetting) noexcept
{
    return std::bit_cast<float>(static_cast<uint32_t>(aSetting.data));
}

// GetIntimidateSuccess and GetBribeSuccess on the NPC in the subject, with the character as the player (the engine's
// functions read g_thePlayer: Actor::WillIntimidateSucceed 37500, the bribe's 37498); none for another shape.
std::optional<bool> SpeechCheckFor(const TESConditionItem& acItem, const ConditionCheckParams& acParams, Actor& aCharacter) noexcept
{
    using JudgedRules::kGetBribeSuccess, JudgedRules::kGetIntimidateSuccess;
    if ((acItem.function != kGetBribeSuccess && acItem.function != kGetIntimidateSuccess) ||
        acItem.runOn != TESConditionItem::RunOn::kSubject || acItem.flags.swapTarget)
        return std::nullopt;
    auto* pNpc = Cast<Actor>(acParams.actionRef);
    auto* pBase = pNpc ? Cast<TESActorBase>(pNpc->baseForm) : nullptr;
    if (!pBase)
        return std::nullopt;
    constexpr uint32_t cSpeech = 17, cMorality = 3;
    const JudgedRules::SpeechCheck cCheck{.CharacterSpeech = aCharacter.GetActorValue(cSpeech),
                                          .CharacterLevel = aCharacter.GetLevel(),
                                          .NpcSpeech = pNpc->GetActorValue(cSpeech),
                                          .NpcLevel = pNpc->GetLevel(),
                                          .NpcMorality = pNpc->GetActorValue(cMorality),
                                          .NpcConfidence = pBase->aiForm.GetConfidence()};
    // BGSEntryPoint's handler (23526): the perk owner's entries adjust the value against the NPC.
    using TApplyPerks = void(uint32_t, Actor*, Actor*, float*);
    POINTER_SKYRIMSE(TApplyPerks, s_applyPerks, 23526);
    bool succeeds;
    if (acItem.function == kGetIntimidateSuccess)
    {
        POINTER_SKYRIMSE(Setting, s_curve, 370633);
        POINTER_SKYRIMSE(Setting, s_cowardly, 370636);
        POINTER_SKYRIMSE(Setting, s_cautious, 370639);
        POINTER_SKYRIMSE(Setting, s_average, 370642);
        POINTER_SKYRIMSE(Setting, s_brave, 370645);
        POINTER_SKYRIMSE(Setting, s_foolhardy, 370648);
        const JudgedRules::IntimidateSettings cSettings{
            .SpeechcraftCurve = SettingOf(*s_curve),
            .ConfidenceMults = {SettingOf(*s_cowardly), SettingOf(*s_cautious), SettingOf(*s_average), SettingOf(*s_brave),
                                SettingOf(*s_foolhardy)}};
        float strength = JudgedRules::IntimidateStrength(cCheck, cSettings);
        s_applyPerks(43, &aCharacter, pNpc, &strength);
        succeeds = strength >= JudgedRules::IntimidateResistance(cCheck, cSettings);
    }
    else
    {
        POINTER_SKYRIMSE(Setting, s_morality, 370630);
        POINTER_SKYRIMSE(Setting, s_npcLevel, 370618);
        POINTER_SKYRIMSE(Setting, s_speechcraft, 370627);
        POINTER_SKYRIMSE(Setting, s_costCurve, 370624);
        POINTER_SKYRIMSE(Setting, s_scale, 370621);
        float amount = JudgedRules::BribeAmount(cCheck, {.MoralityMult = SettingOf(*s_morality),
                                                          .NpcLevelMult = SettingOf(*s_npcLevel),
                                                          .SpeechcraftMult = SettingOf(*s_speechcraft),
                                                          .CostCurve = SettingOf(*s_costCurve),
                                                          .Scale = SettingOf(*s_scale)});
        s_applyPerks(46, &aCharacter, pNpc, &amount);
        succeeds = aCharacter.GetGoldAmount() >= static_cast<int32_t>(amount);
    }
    const float cComparison = acItem.flags.global ? (acItem.comparison.global ? acItem.comparison.global->f : 0.f)
                                                  : acItem.comparison.value;
    return JudgedRules::Compares(succeeds ? 1.f : 0.f, acItem.flags.opCode, cComparison);
}

// The item judged for the character: the player it reads is that character.
__declspec(noinline) bool JudgedIsTrue(const TESConditionItem* apItem, ConditionCheckParams& aParams, TESObjectREFR* pCharacter)
{
    auto* pPlayer = static_cast<TESObjectREFR*>(PlayerCharacter::Get());
    const JudgedRules::Item cFacts{.Function = apItem->function,
                                   .OnPlayer = FixedRunOn(*apItem, aParams) == pPlayer,
                                   .PlayerParam = {apItem->params[0] == pPlayer, apItem->params[1] == pPlayer}};
    // The subject or target the params name (the story manager's are the character already).
    TESObjectREFR* const pSubject = aParams.actionRef;
    TESObjectREFR* const pTarget = aParams.targetRef;
    if (pSubject == pPlayer)
        aParams.actionRef = pCharacter;
    if (pTarget == pPlayer)
        aParams.targetRef = pCharacter;

    bool result;
    auto* pActor = Cast<Actor>(pCharacter);
    if (const auto cSpeech = pActor ? SpeechCheckFor(*apItem, aParams, *pActor) : std::nullopt)
        result = *cSpeech;
    else if (const auto cJudged = JudgedRules::For(cFacts))
    {
        TESConditionItem item = *apItem;
        item.next = nullptr;
        item.function = cJudged->Function;
        if (cJudged->OnCharacter)
        {
            item.runOn = TESConditionItem::RunOn::kReference;
            item.runOnRefHandle = pCharacter->GetHandle().handle.iBits;
        }
        for (size_t i = 0; i < 2; ++i)
            if (cJudged->CharacterParam[i])
                item.params[i] = pCharacter;
        result = RealIsTrue(&item, aParams);
    }
    else
        result = RealIsTrue(apItem, aParams);
    aParams.actionRef = pSubject;
    aParams.targetRef = pTarget;
    return result;
}

// TESConditionItem::IsTrue (29924), every condition on every thread: passed through but while judging.
bool HookIsTrue(const TESConditionItem* apItem, ConditionCheckParams& aParams)
{
    if (TESObjectREFR* pCharacter = t_pJudged)
        return JudgedIsTrue(apItem, aParams, pCharacter);
    return RealIsTrue(apItem, aParams);
}

Hooks::Initializer s_judgedHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(29924, &RealIsTrue, HookIsTrue);
        // On every condition's stack: a lock report there is the engine's unless a judgement runs (JudgedIsTrue).
        LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&HookIsTrue), 0x100);
    });
} // namespace

namespace JudgedFor
{
Scope::Scope(TESObjectREFR* apCharacter) noexcept
    : m_pPrevious(t_pJudged)
{
    t_pJudged = apCharacter;
}

Scope::~Scope() noexcept
{
    t_pJudged = m_pPrevious;
}

TESObjectREFR* Get() noexcept
{
    return t_pJudged;
}
} // namespace JudgedFor
