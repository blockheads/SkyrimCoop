#include <Components/TESCondition.h>
#include <VersionDb.h>

bool TESConditionItem::IsTrue(ConditionCheckParams& aParams) const noexcept
{
    using TIsTrue = bool(const TESConditionItem*, ConditionCheckParams& aParams);
    POINTER_SKYRIMSE(TIsTrue, s_isTrue, 29924);
    return (s_isTrue.Get())(this, aParams);
}

bool TESCondition::IsTrue(TESObjectREFR* apSubject, TESObjectREFR* apTarget) const noexcept
{
    using TIsTrue = bool(const TESCondition*, TESObjectREFR* apSubject, TESObjectREFR* apTarget);
    POINTER_SKYRIMSE(TIsTrue, s_isTrue, 29888);
    return (s_isTrue.Get())(this, apSubject, apTarget);
}
