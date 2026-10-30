#pragma once

#include "AnimationTrace.h"
#include "BodyTrace.h"
#include "CommandServer.h"
#include "ControlFiles.h"
#include "DialogueTrace.h"
#include "EngineEvents.h"
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
    DialogueTrace Dialogue;
    EngineEvents Events;
    InputTrace Inputs;
    bool CommandsStarted = false;
};
} // namespace TestHarness
