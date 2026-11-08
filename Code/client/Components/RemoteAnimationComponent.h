#pragma once

#include <Games/Animation/GraphRandom.h>
#include <Systems/CopyVariables.h>
#include <optional>

struct RemoteAnimationComponent
{
    std::list<ActionEvent> TimePoints;
    ActionEvent LastRanAction;
    ActionEvent LastProcessedAction;
    // The owner's newest graph variables by owner tick (snapshots and replayed actions).
    AnimationVariables Variables;
    uint64_t VariablesTick = 0;
    // An NPC copy's owner's integers by tick, read at its graph's time (CopyVariables.h).
    CopyVariables::IntegerTimeline Integers;
    // Our updates since the head action was staged for the copy's graph update (ActorExtension::Replay).
    uint8_t StagedUpdates = 0;
    // The owner's newest sync point (GraphRandom.h), kept out of the actions' queue: it never holds one up.
    std::optional<ActionEvent> GraphPoint;
    GraphRandom::Copy Random;
    // A loaded point's counters and the updates left to find them: a loaded graph's nodes activate as it updates.
    std::vector<GraphRandom::Counter> GraphCounters;
    uint8_t CounterUpdates = 0;
};
