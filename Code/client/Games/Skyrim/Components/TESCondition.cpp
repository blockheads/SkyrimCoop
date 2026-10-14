#include <TiltedOnlinePCH.h>

#include <Components/TESCondition.h>

bool TESConditionItem::IsTrue(ConditionCheckParams& aParams) const noexcept
{
    TP_THIS_FUNCTION(TIsTrue, bool, const TESConditionItem, ConditionCheckParams& aParams);
    POINTER_SKYRIMSE(TIsTrue, s_isTrue, 29924);
    return TiltedPhoques::ThisCall(s_isTrue.Get(), this, aParams);
}

bool TESCondition::IsTrue(TESObjectREFR* apSubject, TESObjectREFR* apTarget) const noexcept
{
    TP_THIS_FUNCTION(TIsTrue, bool, const TESCondition, TESObjectREFR* apSubject, TESObjectREFR* apTarget);
    POINTER_SKYRIMSE(TIsTrue, s_isTrue, 29888);
    return TiltedPhoques::ThisCall(s_isTrue.Get(), this, apSubject, apTarget);
}
