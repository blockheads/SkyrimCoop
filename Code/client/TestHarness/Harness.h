#pragma once

#include "AnimationTrace.h"
#include "CommandServer.h"
#include "ControlFiles.h"
#include "EngineEvents.h"
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
    EngineEvents Events;
    bool CommandsStarted = false;
};
} // namespace TestHarness
