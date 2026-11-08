#pragma once

#include <Games/Animation/GraphRandom.h>
#include <optional>

struct RemoteAnimationComponent
{
    std::list<ActionEvent> TimePoints;
    ActionEvent LastRanAction;
    ActionEvent LastProcessedAction;
    // The owner's newest graph variables by owner tick (snapshots and replayed actions).
    AnimationVariables Variables;
    uint64_t VariablesTick = 0;
    // Our updates since the head action was staged for the copy's graph update (ActorExtension::Replay).
    uint8_t StagedUpdates = 0;
    // The owner's newest sync point (GraphRandom.h), kept out of the actions' queue: it never holds one up.
    std::optional<ActionEvent> GraphPoint;
    GraphRandom::Copy Random;
    // A loaded point's counters and the updates left to find them: a loaded graph's nodes activate as it updates.
    std::vector<GraphRandom::Counter> GraphCounters;
    uint8_t CounterUpdates = 0;
    // The owner's machine points (MachineStates.h) received, for the copy's machines.
    std::vector<std::pair<uint64_t, std::string>> MachineStates; // the point's owner tick, its packed states
    bool MachineStatesLost = false; // some dropped unread (cMostWaiting): the copy asks for a baseline
    static constexpr size_t cMostWaiting = 16;
};
