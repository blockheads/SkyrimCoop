#include <TiltedOnlinePCH.h>

#include <Games/Animation/IAnimationGraphManagerHolder.h>

#include <BSAnimationGraphManager.h>

bool IAnimationGraphManagerHolder::SetVariableFloat(BSFixedString* apVariable, float aValue)
{
    using TSetFloatVariable = bool(IAnimationGraphManagerHolder*, BSFixedString*, float);
    POINTER_SKYRIMSE(TSetFloatVariable, InternalSetFloatVariable, 32887);

    return InternalSetFloatVariable(this, apVariable, aValue);
}

bool IAnimationGraphManagerHolder::IsReady()
{
    BSAnimationGraphManager* pAnimationGraph = nullptr;
    const auto result = GetBSAnimationGraph(&pAnimationGraph);

    if (pAnimationGraph)
        pAnimationGraph->Release();

    return result;
}
