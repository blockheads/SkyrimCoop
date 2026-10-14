#pragma once

#include "AnimationTrace.h"
#include "CommandServer.h"
#include "ControlFiles.h"
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
    bool CommandsStarted = false;
};
} // namespace TestHarness
