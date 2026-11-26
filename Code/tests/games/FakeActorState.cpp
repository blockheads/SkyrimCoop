// FakeEngine: an actor's values, state and AI.
#include "FakeEngine.h"
#include <set>
#include <characters/ActorValueChangeEvent.h>
#include <core/World.h>

using namespace PartyRules;

float FakeEngine::GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++ValueReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++MaximumReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorMaxValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    const auto actor = Actors.find(aFormId);
    if (actor == Actors.end())
        return 0.f;
    const auto value = actor->second.TemporaryValues.find(static_cast<uint32_t>(aIndex));
    return value == actor->second.TemporaryValues.end() ? 0.f : value->second;
}

// Its base (SetActorValue's), 0 until set.
float FakeEngine::GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto cBase = it->second.Bases.find(static_cast<uint32_t>(aIndex));
    return cBase != it->second.Bases.end() ? cBase->second : 0.f;
}

void FakeEngine::SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ValueWrites;
        const auto cIndex = static_cast<uint32_t>(aIndex);
        auto& base = it->second.Bases[cIndex];
        const float cDelta = aValue - base;
        if (cDelta != 0.f)
        {
            base = aValue;
            it->second.Values.ActorValuesList[cIndex] += cDelta;
            it->second.Values.ActorMaxValuesList[cIndex] += cDelta;
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        }
    }
}

// Permanent and temporary modifiers move the current; damage cannot heal above their combined maximum.
void FakeEngine::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    auto& values = it->second.Values;
    ++it->second.ValueWrites;
    const auto cBefore = values;
    const auto cIndex = static_cast<uint32_t>(aIndex);
    if (aForceMode == 0)
    {
        values.ActorValuesList[cIndex] += aValue - values.ActorMaxValuesList[cIndex];
        values.ActorMaxValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 1)
    {
        it->second.TemporaryValues[cIndex] += aValue - values.ActorValuesList[cIndex];
        values.ActorValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 2)
        values.ActorValuesList[cIndex] = std::min(aValue, values.ActorMaxValuesList[cIndex] + it->second.TemporaryValues[cIndex]);
    if (cBefore != values)
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
    if (cIndex == kHealth)
        CheckHealth(aFormId, it->second);
}

// Each value listed; the others stay. As Actor::SetActorValues: a pool's maximum and current; any other value's base
// made the current one, its maximum with it.
void FakeEngine::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    const auto cPool = [](uint32_t aIndex) { return aIndex >= kHealth && aIndex <= kStamina; };
    for (const auto& [cIndex, cValue] : acValues.ActorMaxValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 0, cIndex, cValue);
    for (const auto& [cIndex, cValue] : acValues.ActorTemporaryValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 1, cIndex, GetActorValue(aFormId, cIndex) + cValue - GetActorTemporaryValue(aFormId, cIndex));
    for (const auto& [cIndex, cValue] : acValues.ActorValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 2, cIndex, cValue);
        else
            SetActorValue(aFormId, cIndex, cValue);
}
