#include <Animation/IAnimationGraphManagerHolder.h>

#include <BSAnimationGraphManager.h>
#include <animation/GraphReference.h>
#include <VersionDb.h>

bool IAnimationGraphManagerHolder::IsReady()
{
    return WithGraphReference<BSAnimationGraphManager>(
        [this](BSAnimationGraphManager** apManager) { return GetBSAnimationGraph(apManager); },
        [](BSAnimationGraphManager*) { return true; });
}
