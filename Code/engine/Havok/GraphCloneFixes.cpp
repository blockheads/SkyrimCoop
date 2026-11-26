#include <Havok/hkbNode.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>

// coop_hkbEventsFromRangeModifier_CopyForClone (59723), the clone's copy constructor (cloneNode 59727): it never copies
// inputValue or lowerBound, so an unbound one keeps the allocation's bytes, different on each game, and decides the
// node's range events (docs/design/graph-input-replay.md, "The turn divergence"). Each clone takes its template's.
namespace
{
using TCopyForClone = hkbEventsFromRangeModifier*(hkbEventsFromRangeModifier*, const hkbEventsFromRangeModifier*);
TCopyForClone* RealCopyForClone = nullptr;

hkbEventsFromRangeModifier* HookCopyForClone(hkbEventsFromRangeModifier* apClone, const hkbEventsFromRangeModifier* apTemplate)
{
    hkbEventsFromRangeModifier* pClone = RealCopyForClone(apClone, apTemplate);
    if (pClone && apTemplate)
    {
        pClone->inputValue = apTemplate->inputValue;
        pClone->lowerBound = apTemplate->lowerBound;
    }
    return pClone;
}

Hooks::Initializer s_copyForCloneHook([]() { HOOK_ID(59723, &RealCopyForClone, HookCopyForClone); });
} // namespace
