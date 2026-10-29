#include <Games/Animation/IAnimationGraphManagerHolder.h>

#include <BSAnimationGraphManager.h>
#include <Games/Animation/GraphReference.h>
#include <VersionDb.h>

bool IAnimationGraphManagerHolder::SetVariableFloat(BSFixedString* apVariable, float aValue)
{
    using TSetFloatVariable = bool(IAnimationGraphManagerHolder*, BSFixedString*, float);
    POINTER_SKYRIMSE(TSetFloatVariable, InternalSetFloatVariable, 32887);

    return InternalSetFloatVariable(this, apVariable, aValue);
}

bool IAnimationGraphManagerHolder::IsReady()
{
    return WithGraphReference<BSAnimationGraphManager>(
        [this](BSAnimationGraphManager** apManager) { return GetBSAnimationGraph(apManager); },
        [](BSAnimationGraphManager*) { return true; });
}
