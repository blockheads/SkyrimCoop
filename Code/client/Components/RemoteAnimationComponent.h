#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

struct RemoteAnimationComponent
{
    List<ActionEvent> TimePoints;
    ActionEvent LastRanAction;
    ActionEvent LastProcessedAction;
    // The owner's newest graph variables by owner tick (snapshots and replayed actions).
    AnimationVariables Variables;
    uint64_t VariablesTick = 0;
    // The head action's variables are in the graph and it has updated once since.
    bool HeadVariablesLoaded = false;
};
