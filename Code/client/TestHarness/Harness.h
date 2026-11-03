#pragma once

#include "Actions.h"
#include "AnimationTrace.h"
#include "BodyTrace.h"
#include "CommandServer.h"
#include "ControlFiles.h"
#include "DialogueTrace.h"
#include "EngineEvents.h"
#include "FrameStepper.h"
#include "GaitWatch.h"
#include "InputTrace.h"
#include "PlayerInput.h"

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
    DialogueTrace Dialogue;
    EngineEvents Events;
    InputTrace Inputs;
    class Actions Actions;
    FrameStepper Stepper;
    bool CommandsStarted = false;
};
} // namespace TestHarness
