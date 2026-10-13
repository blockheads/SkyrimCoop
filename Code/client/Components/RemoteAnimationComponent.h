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
    // Our updates since the head action was staged for the copy's graph update (ActorExtension::Replay).
    uint8_t StagedUpdates = 0;
};
