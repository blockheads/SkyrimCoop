#pragma once

#include "TestHarness/Actions.h"
#include "TestHarness/AnimationTrace.h"
#include "TestHarness/BodyTrace.h"
#include "TestHarness/CommandServer.h"
#include "TestHarness/ControlFiles.h"
#include "TestHarness/DialogueTrace.h"
#include "TestHarness/EngineEvents.h"
#include "TestHarness/FrameStepper.h"
#include "TestHarness/FrozenActors.h"
#include "TestHarness/GaitWatch.h"
#include "TestHarness/InputTrace.h"
#include "TestHarness/JitterWatch.h"
#include "TestHarness/PlayerInput.h"
#include "TestHarness/Scheduled.h"
#include "TestHarness/Terrain.h"

namespace TestHarness
{
// The harness's state. Game thread only.
struct Harness
{
    static Harness& Get();

    ControlFiles Files;
    PlayerInput Input;
    CommandServer Commands;
    AnimationTrace Trace;
    BodyTrace Bodies;
    GaitWatch Gait;
    JitterWatch Jitter;
    DialogueTrace Dialogue;
    EngineEvents Events;
    InputTrace Inputs;
    class Actions Actions;
    FrameStepper Stepper;
    FrozenActors Frozen;
    TerrainScan Terrain;
    Scheduled Schedule;
    bool CommandsStarted = false;
};
} // namespace TestHarness
