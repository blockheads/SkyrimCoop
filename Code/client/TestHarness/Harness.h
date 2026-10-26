#pragma once

#include "AnimationTrace.h"
#include "BodyTrace.h"
#include "CommandServer.h"
#include "ControlFiles.h"
#include "EngineEvents.h"
#include "InputTrace.h"
#include "LogTrail.h"
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
    EngineEvents Events;
    LogTrail Logs;
    InputTrace Inputs;
    bool CommandsStarted = false;
};
} // namespace TestHarness
